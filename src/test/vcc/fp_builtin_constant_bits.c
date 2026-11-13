/* Builtin FP constants retain payload and signaling bits in runtime expressions. */
__attribute__((noinline)) static unsigned fbits(int signaling) {
 union {float f; unsigned u;} v;
 v.f=signaling ? __builtin_nansf("0x123") : __builtin_nanf("0x456");
 return v.u;
}
__attribute__((noinline)) static unsigned long long dbits(int signaling) {
 union {double f; unsigned long long u;} v;
 v.f=signaling ? __builtin_nans("0x123") : __builtin_nan("0x456");
 return v.u;
}
int main(void) {
 if(fbits(0)!=0x7fc00456u || fbits(1)!=0x7f800123u) return 1;
 if(dbits(0)!=0x7ff8000000000456ull || dbits(1)!=0x7ff0000000000123ull) return 2;
#if defined(__wasm__)
 union {long double f; unsigned __int128 u;} v;
 v.f=__builtin_nansl("0x123");
 if(v.u!=((unsigned __int128)0x7fff<<112|0x123)) return 3;
 v.f=__builtin_nanl("0x456");
 if(v.u!=((unsigned __int128)0x7fff8<<108|0x456)) return 4;
 v.f=__builtin_infl();
 if(v.u!=((unsigned __int128)0x7fff<<112)) return 5;
#endif
 return 0;
}
