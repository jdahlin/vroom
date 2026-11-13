/* diag-flags: --target=x86_64-linux-gnu -msse4.2 */
typedef char bytes __attribute__((vector_size(16)));
int side(void);
int variable(bytes a, bytes b, int control) {
    return __builtin_ia32_pcmpistri128(a, b, control);
}
int side_effect(bytes a, bytes b) {
    return __builtin_ia32_pcmpistri128(a, b, (side(), 0));
}
