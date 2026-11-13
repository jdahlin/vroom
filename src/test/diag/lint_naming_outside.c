/* diag-vpp-flags: --lint */
/* diag-differs: --lint is vcc's own mode; clang has no such check */
/* A file outside the module table (test/, bin/, tools/) is not checked: no finding. */
#line 1 "src/test/vcc/lint_naming_outside.c"
int anything_goes(int x) { return x; }
int counter;
