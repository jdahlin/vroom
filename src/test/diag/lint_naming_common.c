/* diag-vpp-flags: --lint */
/* diag-differs: --lint is vcc's own mode; clang has no such check */
/* common/ and port/: "v" + facility + "_" (doc/STYLE.md, Module prefixes). */
#line 1 "src/common/lint_naming_common.c"
int vbuffer_length(int x) { return x; }
int v_empty_facility(int x) { return x; }
int vend(int x) { return x; }
int buffer_length(int x) { return x; }
