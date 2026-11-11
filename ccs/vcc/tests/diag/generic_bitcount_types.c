int signed_input(int x) { return __builtin_clzg(x); }
int floating_input(double x) { return __builtin_popcountg(x); }
int long_fallback(unsigned x) { return __builtin_clzg(x, 1L); }
int unsigned_fallback(unsigned x) { return __builtin_ctzg(x, 1U); }
int floating_fallback(unsigned x) { return __builtin_clzg(x, 1.0); }
int bitint_fallback(unsigned x) { return __builtin_ctzg(x, (_BitInt(32))1); }
