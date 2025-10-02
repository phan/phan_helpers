/*
  +----------------------------------------------------------------------+
  | Phan Helpers Extension                                               |
  +----------------------------------------------------------------------+
*/

#ifndef PHP_PHAN_HELPERS_H
#define PHP_PHAN_HELPERS_H

extern zend_module_entry phan_helpers_module_entry;
#define phpext_phan_helpers_ptr &phan_helpers_module_entry

#define PHP_PHAN_HELPERS_VERSION "0.1.0"

#ifdef PHP_WIN32
#   define PHP_PHAN_HELPERS_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#   define PHP_PHAN_HELPERS_API __attribute__ ((visibility("default")))
#else
#   define PHP_PHAN_HELPERS_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_FUNCTION(phan_unique_types);
PHP_MINFO_FUNCTION(phan_helpers);

#endif  /* PHP_PHAN_HELPERS_H */
