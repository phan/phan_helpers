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
#include "ext/hash/php_hash.h"
#include "ext/hash/php_hash_xxhash.h"
#include "Zend/zend_smart_str.h"
#include "php_phan_helpers.h"
#include "phan_helpers_arginfo.h"
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

/* Forward declarations for XXH3-128-based hashing with normalization */
static void phan_hash_node_xxh3(smart_str *str, zval *node);
static void phan_hash_value_xxh3(smart_str *str, zval *val);
static void phan_hash_key_xxh3(smart_str *str, zend_ulong idx, zend_string *key);

/* {{{ phan_hash_key_xxh3
 * Hash a key the same way PHP does: XXH3-128 for strings, packed format for integers
 */
static void phan_hash_key_xxh3(smart_str *str, zend_ulong idx, zend_string *key)
{
    if (key) {
        /* String key - hash with XXH3-128 */
        PHP_XXH3_128_CTX context;
        unsigned char digest[16];
        PHP_XXH3_128_Init(&context, NULL);
        PHP_XXH3_128_Update(&context, (unsigned char *)ZSTR_VAL(key), ZSTR_LEN(key));
        PHP_XXH3_128_Final(digest, &context);
        smart_str_appendl(str, (char *)digest, 16);
    } else {
        /* Integer key - pack as 16 bytes (8 zeros + 8 byte integer) */
        char packed[16] = {0};
        #if SIZEOF_ZEND_LONG == 8
            memcpy(packed + 8, &idx, 8);
        #else
            uint32_t idx32 = (uint32_t)idx;
            memcpy(packed + 12, &idx32, 4);
        #endif
        smart_str_appendl(str, packed, 16);
    }
}
/* }}} */

/* {{{ phan_hash_value_xxh3
 * Hash a value the same way PHP does
 */
static void phan_hash_value_xxh3(smart_str *str, zval *val)
{
    if (val == NULL || Z_TYPE_P(val) == IS_NULL) {
        /* NULL - return fixed 16-byte pattern */
        smart_str_appendl(str, "\0\0\0\0\0\0\0\x02\0\0\0\0\0\0\0\0", 16);
    } else if (Z_TYPE_P(val) == IS_STRING) {
        /* String - hash with XXH3-128 */
        PHP_XXH3_128_CTX context;
        unsigned char digest[16];
        PHP_XXH3_128_Init(&context, NULL);
        PHP_XXH3_128_Update(&context, (unsigned char *)Z_STRVAL_P(val), Z_STRLEN_P(val));
        PHP_XXH3_128_Final(digest, &context);
        smart_str_appendl(str, (char *)digest, 16);
    } else if (Z_TYPE_P(val) == IS_LONG) {
        /* Integer - pack as 16 bytes */
        char packed[16] = {0};
        zend_long lval = Z_LVAL_P(val);
        #if SIZEOF_ZEND_LONG == 8
            /* memcpy(packed + 8, &Z_LVAL_P(val), 8); */
            /* Big-endian version instead */
            packed[8]  = (lval >> 56) & 0xFF;
            packed[9]  = (lval >> 48) & 0xFF;
            packed[10] = (lval >> 40) & 0xFF;
            packed[11] = (lval >> 32) & 0xFF;
            packed[12] = (lval >> 24) & 0xFF;
            packed[13] = (lval >> 16) & 0xFF;
            packed[14] = (lval >> 8) & 0xFF;
            packed[15] = lval & 0xFF;
        #else
            /*
            int32_t val32 = (int32_t)Z_LVAL_P(val);
            memcpy(packed + 12, &val32, 4);
            */
            int32_t val32 = (int32_t)lval;
            packed[12] = (val32 >> 24) & 0xFF;
            packed[13] = (val32 >> 16) & 0xFF;
            packed[14] = (val32 >> 8) & 0xFF;
            packed[15] = val32 & 0xFF;
        #endif
        smart_str_appendl(str, packed, 16);
    } else if (Z_TYPE_P(val) == IS_DOUBLE) {
        /* Float - pack as 16 bytes with marker */
        char packed[16];
        memset(packed, 0, 8);
        packed[7] = 1;
        double dval = Z_DVAL_P(val);
        memcpy(packed + 8, &dval, 8);
        smart_str_appendl(str, packed, 16);
    } else if (Z_TYPE_P(val) == IS_OBJECT) {
        /* AST Node - hash it first, then append the hash (like PHP does) */
        smart_str child_str = {0};
        PHP_XXH3_128_CTX context;
        unsigned char digest[16];

        phan_hash_node_xxh3(&child_str, val);
        smart_str_0(&child_str);

        /* Hash the child's string representation */
        PHP_XXH3_128_Init(&context, NULL);
        PHP_XXH3_128_Update(&context, (unsigned char *)ZSTR_VAL(child_str.s), ZSTR_LEN(child_str.s));
        PHP_XXH3_128_Final(digest, &context);

        smart_str_free(&child_str);
        smart_str_appendl(str, (char *)digest, 16);
    } else {
        /* Unknown type - return fixed pattern */
        smart_str_appendl(str, "\0\0\0\0\0\0\0\x01\0\0\0\0\0\0\0\0", 16);
    }
}
/* }}} */

/* {{{ phan_hash_node_xxh3
 * Recursively hash an AST Node the same way PHP does
 */
static void phan_hash_node_xxh3(smart_str *str, zval *node)
{
    zend_object *obj;
    zval *kind_zv, *flags_zv, *children_zv;
    zend_string *key;
    zval *child;
    zend_ulong idx;
    HashTable *props;

    if (Z_TYPE_P(node) != IS_OBJECT) {
        phan_hash_value_xxh3(str, node);
        return;
    }

    obj = Z_OBJ_P(node);
    props = obj->handlers->get_properties(obj);
    if (!props) {
        return;
    }

    /* Get and dereference properties */
    kind_zv = zend_hash_str_find(props, "kind", sizeof("kind") - 1);
    flags_zv = zend_hash_str_find(props, "flags", sizeof("flags") - 1);
    children_zv = zend_hash_str_find(props, "children", sizeof("children") - 1);

    if (kind_zv) {
        ZVAL_DEREF(kind_zv);
        if (Z_TYPE_P(kind_zv) == IS_INDIRECT) kind_zv = Z_INDIRECT_P(kind_zv);
    }
    if (flags_zv) {
        ZVAL_DEREF(flags_zv);
        if (Z_TYPE_P(flags_zv) == IS_INDIRECT) flags_zv = Z_INDIRECT_P(flags_zv);
    }
    if (children_zv) {
        ZVAL_DEREF(children_zv);
        if (Z_TYPE_P(children_zv) == IS_INDIRECT) {
            children_zv = Z_INDIRECT_P(children_zv);
            /* After dereferencing IS_INDIRECT, we may have an IS_REFERENCE, so deref again */
            ZVAL_DEREF(children_zv);
        }
    }

    /* Build string: "N" + kind + ":" + flags */
    smart_str_appendc(str, 'N');
    if (kind_zv && Z_TYPE_P(kind_zv) == IS_LONG) {
        char kind_buf[32];
        int kind_len = snprintf(kind_buf, sizeof(kind_buf), "%ld", Z_LVAL_P(kind_zv));
        smart_str_appendl(str, kind_buf, kind_len);
    }
    smart_str_appendc(str, ':');
    if (flags_zv && Z_TYPE_P(flags_zv) == IS_LONG) {
        char flags_buf[32];
        zend_long flags_masked = Z_LVAL_P(flags_zv) & 0x3ffffff;
        int flags_len = snprintf(flags_buf, sizeof(flags_buf), "%ld", flags_masked);
        smart_str_appendl(str, flags_buf, flags_len);
    }

    /* Process children */
    if (children_zv && Z_TYPE_P(children_zv) == IS_ARRAY) {
        HashTable *children_ht = Z_ARRVAL_P(children_zv);

        ZEND_HASH_FOREACH_KEY_VAL(children_ht, idx, key, child) {
            /* Skip keys starting with "phan" */
            if (key && ZSTR_LEN(key) >= 4 && memcmp(ZSTR_VAL(key), "phan", 4) == 0) {
                continue;
            }

            /* Dereference child */
            ZVAL_DEREF(child);
            if (Z_TYPE_P(child) == IS_INDIRECT) {
                child = Z_INDIRECT_P(child);
            }

            /* Hash key and value */
            phan_hash_key_xxh3(str, idx, key);
            phan_hash_value_xxh3(str, child);
        } ZEND_HASH_FOREACH_END();
    }
}
/* }}} */

/* {{{ PHP_FUNCTION(phan_ast_hash)
 * XXH3-128-based hashing of AST nodes matching PHP implementation exactly
 *
 * This is a C implementation of ASTHasher::hash() and computeHash()
 *
 * Arguments:
 *   mixed $node - AST Node object, or primitive value
 *
 * Returns:
 *   string - 16-byte binary XXH3-128 hash
 */
PHP_FUNCTION(phan_ast_hash)
{
    zval *node;
    smart_str str = {0};
    PHP_XXH3_128_CTX context;
    unsigned char digest[16];

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(node)
    ZEND_PARSE_PARAMETERS_END();

    /* Handle non-objects directly */
    if (!Z_ISREF_P(node) && Z_TYPE_P(node) != IS_OBJECT) {
        phan_hash_value_xxh3(&str, node);
        smart_str_0(&str);

        /* Hash the built string */
        PHP_XXH3_128_Init(&context, NULL);
        PHP_XXH3_128_Update(&context, (unsigned char *)ZSTR_VAL(str.s), ZSTR_LEN(str.s));
        PHP_XXH3_128_Final(digest, &context);

        smart_str_free(&str);
        RETURN_STRINGL((char *)digest, 16);
    }

    /* For objects, hash the node string */
    phan_hash_node_xxh3(&str, node);
    smart_str_0(&str);

    /* Hash the built string with XXH3-128 */
    PHP_XXH3_128_Init(&context, NULL);
    PHP_XXH3_128_Update(&context, (unsigned char *)ZSTR_VAL(str.s), ZSTR_LEN(str.s));
    PHP_XXH3_128_Final(digest, &context);

    smart_str_free(&str);
    RETURN_STRINGL((char *)digest, 16);
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
