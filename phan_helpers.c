/*
  +----------------------------------------------------------------------+
  | Phan Helpers Extension                                               |
  +----------------------------------------------------------------------+
  | Copyright (c) 2025 Rasmus Lerdorf                                    |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Rasmus Lerdorf                                               |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_phan_helpers.h"
#include "phan_helpers_arginfo.h"
#define XXH_INLINE_ALL
#include "ext/hash/xxhash/xxhash.h"
#include "Zend/zend_exceptions.h"

/* {{{ PHP_FUNCTION(phan_unique_types)
 * Fast deduplication of Type objects using object IDs
 *
 * This is a C implementation of UnionType::getUniqueTypes()
 *
 * Arguments:
 *   array $type_list - Array of Type objects to deduplicate
 *
 * Returns:
 *   array - Deduplicated array of Type objects
 */
PHP_FUNCTION(phan_unique_types)
{
    zval *type_list;
    HashTable *ht;
    zval *entry;
    HashTable *seen_ids;
    zend_ulong num_types;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(type_list)
    ZEND_PARSE_PARAMETERS_END();

    ht = Z_ARRVAL_P(type_list);
    num_types = zend_hash_num_elements(ht);

    /* Empty array optimization */
    if (num_types == 0) {
        RETURN_EMPTY_ARRAY();
    }

    /* Single element optimization */
    if (num_types == 1) {
        RETURN_COPY(type_list);
    }

    /* Initialize result array */
    array_init(return_value);

    /* Use hash table to track seen object IDs for O(1) lookups */
    ALLOC_HASHTABLE(seen_ids);
    zend_hash_init(seen_ids, num_types, NULL, NULL, 0);

    /* Iterate through input array */
    ZEND_HASH_FOREACH_VAL(ht, entry) {
        if (Z_TYPE_P(entry) == IS_OBJECT) {
            /* Get object ID (handle) */
            zend_ulong obj_id = Z_OBJ_HANDLE_P(entry);

            /* Check if we've seen this object ID before */
            if (!zend_hash_index_exists(seen_ids, obj_id)) {
                /* Mark as seen (value doesn't matter, we just need the key) */
                zend_hash_index_add_empty_element(seen_ids, obj_id);

                /* Add to result array */
                Z_TRY_ADDREF_P(entry);
                add_next_index_zval(return_value, entry);
            }
        } else {
            /* Non-object values - shouldn't happen in normal Phan usage,
             * but handle gracefully by using identity comparison fallback */
            zval *result_entry;
            zend_bool found = 0;

            ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(return_value), result_entry) {
                if (zend_is_identical(entry, result_entry)) {
                    found = 1;
                    break;
                }
            } ZEND_HASH_FOREACH_END();

            if (!found) {
                Z_TRY_ADDREF_P(entry);
                add_next_index_zval(return_value, entry);
            }
        }
    } ZEND_HASH_FOREACH_END();

    /* Cleanup */
    zend_hash_destroy(seen_ids);
    FREE_HASHTABLE(seen_ids);
}
/* }}} */

/* Forward declarations for AST hashing */
static void phan_hash_node_recursive(XXH3_state_t* state, zval *node);
static void phan_hash_value(XXH3_state_t* state, zval *val);

/* {{{ phan_hash_value
 * Hash a primitive value (string, int, float, null) into XXH128 state
 */
static void phan_hash_value(XXH3_state_t* state, zval *val)
{
    switch (Z_TYPE_P(val)) {
        case IS_STRING:
            XXH3_128bits_update(state, Z_STRVAL_P(val), Z_STRLEN_P(val));
            break;
        case IS_LONG: {
            /* Pack integer as 8 bytes (little-endian) */
            uint64_t num = (uint64_t)Z_LVAL_P(val);
            XXH3_128bits_update(state, &num, sizeof(num));
            break;
        }
        case IS_DOUBLE: {
            /* Pack double as 8 bytes */
            double num = Z_DVAL_P(val);
            XXH3_128bits_update(state, &num, sizeof(num));
            break;
        }
        case IS_NULL:
            /* Hash a marker byte for null */
            XXH3_128bits_update(state, "\x00", 1);
            break;
        case IS_OBJECT:
            /* This is an AST node */
            phan_hash_node_recursive(state, val);
            break;
        default:
            /* Shouldn't happen, but hash type ID */
            {
                unsigned char type = Z_TYPE_P(val);
                XXH3_128bits_update(state, &type, 1);
            }
            break;
    }
}
/* }}} */

/* {{{ phan_hash_node_recursive
 * Recursively hash an AST Node object
 *
 * ast\Node objects store properties that we need to hash.
 * We use proper property lookup to avoid issues with OPcache optimization.
 */
static void phan_hash_node_recursive(XXH3_state_t* state, zval *node)
{
    zend_object *obj;
    zval *kind_zv, *flags_zv, *children_zv;
    zend_string *key;
    zval *child;
    static int debug_enabled = -1;
    HashTable *props;

    if (Z_TYPE_P(node) != IS_OBJECT) {
        phan_hash_value(state, node);
        return;
    }

    obj = Z_OBJ_P(node);

    /* Check for debug mode once */
    if (debug_enabled == -1) {
        debug_enabled = getenv("PHAN_HELPERS_DEBUG") != NULL;
    }

    /* Get properties table directly - ast\Node has public properties */
    props = obj->handlers->get_properties(obj);
    if (!props) {
        /* Fallback: empty hash for objects without properties */
        XXH3_128bits_update(state, "\x00", 1);
        return;
    }

    kind_zv = zend_hash_str_find(props, "kind", sizeof("kind") - 1);
    flags_zv = zend_hash_str_find(props, "flags", sizeof("flags") - 1);
    children_zv = zend_hash_str_find(props, "children", sizeof("children") - 1);

    /* Dereference if needed - properties may be IS_INDIRECT or IS_REFERENCE */
    if (kind_zv) {
        ZVAL_DEREF(kind_zv);  /* Handle IS_REFERENCE */
        if (Z_TYPE_P(kind_zv) == IS_INDIRECT) {  /* Handle IS_INDIRECT */
            kind_zv = Z_INDIRECT_P(kind_zv);
        }
    }
    if (flags_zv) {
        ZVAL_DEREF(flags_zv);
        if (Z_TYPE_P(flags_zv) == IS_INDIRECT) {
            flags_zv = Z_INDIRECT_P(flags_zv);
        }
    }
    if (children_zv) {
        ZVAL_DEREF(children_zv);
        if (Z_TYPE_P(children_zv) == IS_INDIRECT) {
            children_zv = Z_INDIRECT_P(children_zv);
        }
    }

    /* Hash kind property */
    if (kind_zv && Z_TYPE_P(kind_zv) == IS_LONG) {
        uint64_t kind = (uint64_t)Z_LVAL_P(kind_zv);
        XXH3_128bits_update(state, "N", 1);  /* Node marker */
        XXH3_128bits_update(state, &kind, sizeof(kind));
    }

    /* Hash flags property (masked to 20 bits like PHP version) */
    if (flags_zv && Z_TYPE_P(flags_zv) == IS_LONG) {
        uint64_t flags = (uint64_t)(Z_LVAL_P(flags_zv) & 0xfffff);
        XXH3_128bits_update(state, ":", 1);
        XXH3_128bits_update(state, &flags, sizeof(flags));
    }

    /* Hash children array */
    if (children_zv && Z_TYPE_P(children_zv) == IS_ARRAY) {
        HashTable *children_ht = Z_ARRVAL_P(children_zv);
        zend_ulong idx;
        int child_count = 0;

        /* Iterate through children */
        ZEND_HASH_FOREACH_KEY_VAL(children_ht, idx, key, child) {
            /* Skip keys starting with "phan" (added by PhanAnnotationAdder) */
            if (key && ZSTR_LEN(key) >= 4 && memcmp(ZSTR_VAL(key), "phan", 4) == 0) {
                if (debug_enabled) {
                    fprintf(stderr, "[phan_helpers]   Skipping phan-annotated key: %s\n", ZSTR_VAL(key));
                }
                continue;
            }

            if (debug_enabled) {
                if (key) {
                    fprintf(stderr, "[phan_helpers]   Child #%d key=\"%s\"\n", child_count, ZSTR_VAL(key));
                } else {
                    fprintf(stderr, "[phan_helpers]   Child #%d key=%lu\n", child_count, idx);
                }
            }

            /* Hash the key */
            if (key) {
                /* String key */
                XXH3_128bits_update(state, ZSTR_VAL(key), ZSTR_LEN(key));
            } else {
                /* Integer key */
                XXH3_128bits_update(state, &idx, sizeof(idx));
            }

            /* Recursively hash the child value */
            phan_hash_value(state, child);
            child_count++;
        } ZEND_HASH_FOREACH_END();

        if (debug_enabled) {
            fprintf(stderr, "[phan_helpers]   Total children hashed: %d\n", child_count);
        }
    }
}
/* }}} */

/* {{{ PHP_FUNCTION(phan_ast_hash)
 * Fast XXH128 hashing of AST nodes
 *
 * This is a C implementation of ASTHasher::hash() and computeHash()
 *
 * Arguments:
 *   mixed $node - AST Node object, or primitive value
 *
 * Returns:
 *   string - 16-byte binary XXH128 hash
 */
PHP_FUNCTION(phan_ast_hash)
{
    zval *node;
    XXH3_state_t* state;
    XXH128_hash_t hash;
    static int debug_enabled = -1;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(node)
    ZEND_PARSE_PARAMETERS_END();

    /* Check for debug mode once */
    if (debug_enabled == -1) {
        debug_enabled = getenv("PHAN_HELPERS_DEBUG") != NULL;
    }

    /* Initialize XXH3 state for 128-bit hashing */
    state = XXH3_createState();
    if (!state) {
        zend_throw_exception(zend_ce_exception, "Failed to create XXH3 state", 0);
        RETURN_FALSE;
    }

    if (XXH3_128bits_reset(state) == XXH_ERROR) {
        XXH3_freeState(state);
        zend_throw_exception(zend_ce_exception, "Failed to reset XXH3 state", 0);
        RETURN_FALSE;
    }

    if (debug_enabled) {
        fprintf(stderr, "[phan_helpers] === phan_ast_hash() called ===\n");
        if (Z_TYPE_P(node) == IS_OBJECT) {
            fprintf(stderr, "[phan_helpers] Input: Object (handle %u)\n", Z_OBJ_HANDLE_P(node));
        } else {
            fprintf(stderr, "[phan_helpers] Input: Non-object (type %d)\n", Z_TYPE_P(node));
        }
    }

    /* Hash the node */
    phan_hash_value(state, node);

    /* Get the final hash */
    hash = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    /* Convert XXH128_hash_t to 16-byte binary string */
    /* XXH128 returns {low64, high64} in native endianness */
    unsigned char result[16];
    memcpy(result, &hash.low64, 8);
    memcpy(result + 8, &hash.high64, 8);

    if (debug_enabled) {
        fprintf(stderr, "[phan_helpers] Output hash (hex): ");
        for (int i = 0; i < 16; i++) {
            fprintf(stderr, "%02x", result[i]);
        }
        fprintf(stderr, "\n\n");
    }

    RETURN_STRINGL((char *)result, 16);
}
/* }}} */

/* {{{ phan_helpers_functions[]
 */
static const zend_function_entry phan_helpers_functions[] = {
    PHP_FE(phan_unique_types, arginfo_phan_unique_types)
    PHP_FE(phan_ast_hash, arginfo_phan_ast_hash)
    PHP_FE_END
};
/* }}} */

/* {{{ phan_helpers_module_entry
 */
zend_module_entry phan_helpers_module_entry = {
    STANDARD_MODULE_HEADER,
    "phan_helpers",
    phan_helpers_functions,
    NULL,                       /* PHP_MINIT */
    NULL,                       /* PHP_MSHUTDOWN */
    NULL,                       /* PHP_RINIT */
    NULL,                       /* PHP_RSHUTDOWN */
    PHP_MINFO(phan_helpers),
    "0.1.0",
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_PHAN_HELPERS
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(phan_helpers)
#endif

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(phan_helpers)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "phan_helpers support", "enabled");
    php_info_print_table_row(2, "Version", "0.1.0");
    php_info_print_table_end();
}
/* }}} */
