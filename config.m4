PHP_ARG_ENABLE(phan_helpers, whether to enable phan_helpers support,
[  --enable-phan_helpers           Enable phan_helpers support])

if test "$PHP_PHAN_HELPERS" != "no"; then
  PHP_NEW_EXTENSION(phan_helpers, phan_helpers.c, $ext_shared)
fi
