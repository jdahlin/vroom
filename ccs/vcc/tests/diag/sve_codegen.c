/* diag-mode: -c */
/* diag-differs: vcc diagnoses unsupported lowering; clang diagnoses missing sve. */
__SVFloat32_t identity(__SVFloat32_t x) { return x; }
