/* Precision-aware counts: expected single-bit positions and independently
 * computed byte-pattern counts, without using a wide arithmetic oracle. */
typedef unsigned _BitInt(129) U129;
typedef unsigned _BitInt(257) U257;
_Static_assert(__builtin_clzg((U129)1) == 128, "clz precision");
_Static_assert(__builtin_clzg((U129)1, 7) == 128, "nonzero constant fallback");
_Static_assert(_Generic(__builtin_popcountg((U257)0), int:1, default:0), "scalar result type");
_Static_assert(__builtin_ctzg((U257)1 << 256) == 256, "ctz high limb");
_Static_assert(__builtin_popcountg(~(U257)0) == 257, "popcount all limbs");
_Static_assert(__builtin_clzg((U129)0, 91) == 91, "zero clz fallback");
_Static_assert(__builtin_ctzg((U257)0, -17) == -17, "zero ctz fallback");
static int calls, fallback_calls;
static int fallback(void) { fallback_calls++; return -23; }
#define CASE(N,PC,LZ,TZ) \
static int test_##N(void) { \
 typedef unsigned _BitInt(N) U; U x=0; unsigned char *p=(unsigned char *)&x; \
 for(int bit=0;bit<N;bit++) { \
  for(unsigned j=0;j<sizeof(x);j++)p[j]=0; p[bit/8]=(unsigned char)(1u<<(bit%8)); \
  calls=0;fallback_calls=0; \
  if(__builtin_clzg((++calls,x),fallback())!=N-1-bit || calls!=1 || fallback_calls!=1)return 1; \
  if(__builtin_ctzg((++calls,x),fallback())!=bit || calls!=2 || fallback_calls!=2)return 2; \
  if(__builtin_popcountg((++calls,x))!=1 || calls!=3)return 3; \
 } \
 for(unsigned j=0;j<sizeof(x);j++)p[j]=0; \
 calls=0;fallback_calls=0; \
 if(__builtin_clzg((++calls,x),fallback())!=-23 || calls!=1 || fallback_calls!=1)return 4; \
 if(__builtin_ctzg((++calls,x),fallback())!=-23 || calls!=2 || fallback_calls!=2)return 5; \
 if(__builtin_popcountg((++calls,x))!=0 || calls!=3)return 6; \
 for(unsigned j=0;j<(N+7)/8;j++)p[j]=(unsigned char)(j*37+11); \
 if(N%8)p[N/8]&=(1u<<(N%8))-1; \
 if(__builtin_popcountg(x)!=PC || __builtin_clzg(x)!=LZ || __builtin_ctzg(x)!=TZ)return 7; \
 return 0; \
}
/* Python oracle: int.from_bytes(bytes((j*37+11)&255 ...), "little") & ((1<<N)-1). */
CASE(129,64,0,0)
CASE(130,65,0,0)
CASE(191,95,0,0)
CASE(192,95,1,0)
CASE(193,96,0,0)
CASE(255,124,4,0)
CASE(256,125,0,0)
CASE(257,126,0,0)
CASE(513,257,0,0)
CASE(4097,2049,0,0)
unsigned wide_counts(void) {
 if(test_129()) return 129;
 if(test_130()) return 130;
 if(test_191()) return 191;
 if(test_192()) return 192;
 if(test_193()) return 193;
 if(test_255()) return 255;
 if(test_256()) return 256;
 if(test_257()) return 257;
 if(test_513()) return 513;
 if(test_4097()) return 4097;
 U129 v=(U129)1<<128;
 fallback_calls=0;
 if(__builtin_ctzg(v++,fallback())!=128 || v!=((U129)1<<128)+1 || fallback_calls!=1)return 3;
 calls=0; fallback_calls=0;
 if(__builtin_clzg((U129)1,fallback())!=128 || fallback_calls!=1)return 2;
 return 0;
}
