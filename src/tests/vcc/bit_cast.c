/* vcc-flags: -std=c23 */
/* vcc-threads-same */
/* Raw representations, aggregate/array values, and exactly-once operands. */
extern void abort(void);
#define CHECK(x) do { if (!(x)) abort(); } while (0)
typedef unsigned char Bytes[8];
typedef unsigned V4 __attribute__((vector_size(16)));
struct Pair { unsigned x,y; };
struct Padded { unsigned char c; unsigned x; };
struct Large { unsigned x[32]; };
union Pun { unsigned u; float f; };
static int calls;
__attribute__((noinline)) static float once(void) { calls++; return 1.0f; }
__attribute__((noinline)) static struct Pair pair(void) { calls++; return (struct Pair){3,7}; }
constexpr unsigned fb = __builtin_bit_cast(unsigned,1.0f);
constexpr struct Pair cp = __builtin_bit_cast(struct Pair,0x0000000700000003ULL);
struct ByteArray { Bytes b; };
constexpr struct ByteArray cb = __builtin_bit_cast(struct ByteArray,0x0102030405060708ULL);
constexpr struct Padded padded = __builtin_bit_cast(struct Padded,0x0000000200000001ULL);
static_assert(fb==0x3f800000u && cp.x==3 && cp.y==7 && cb.b[0]==8 && cb.b[7]==1);
static_assert(padded.c==1 && padded.x==2);
static_assert(__builtin_bit_cast(unsigned,__builtin_bit_cast(float,0x7f800001u))==0x7f800001u);
static_assert(__builtin_bit_cast(signed char,(unsigned char)128)==-128);
/* Unknown padding can pass through a temporary byte object, but becomes
 * indeterminate again when a padded record is the destination. */
struct PadBytes { unsigned char b[8]; };
constexpr struct Padded round_padding = __builtin_bit_cast(struct Padded,
    __builtin_bit_cast(struct PadBytes, (struct Padded){1,2}));
static_assert(round_padding.c == 1 && round_padding.x == 2);
static_assert(__builtin_bit_cast(struct PadBytes, (struct Padded){1,2}).b[0] == 1);
constexpr V4 cv = {1,2,3,4};
struct VecBits { unsigned a,b,c,d; };
constexpr struct VecBits cvec = __builtin_bit_cast(struct VecBits, cv);
static_assert(cvec.a == 1 && cvec.d == 4);
typedef unsigned V2 __attribute__((vector_size(8)));
constexpr struct Pair arithmetic = __builtin_bit_cast(struct Pair, (V2){1,2} + (V2){3,4});
constexpr struct Pair compared = __builtin_bit_cast(struct Pair, (V2){1,2} == (V2){1,3});
static_assert(arithmetic.x == 4 && arithmetic.y == 6 && compared.x == ~0u && compared.y == 0);
constexpr struct Pair shuffled = __builtin_bit_cast(struct Pair, __builtin_shufflevector((V2){1,2},(V2){3,4},3,0));
static_assert(shuffled.x == 4 && shuffled.y == 1);
typedef float F2 __attribute__((vector_size(8)));
constexpr struct Pair converted = __builtin_bit_cast(struct Pair, __builtin_convertvector((F2){3.5f,7.5f},V2));
static_assert(converted.x == 3 && converted.y == 7);
constexpr char text[] = "abc";
static_assert(__builtin_bit_cast(unsigned,text) == 0x636261u);
const unsigned ordinary = 0x3f800000u;
static float folded_extension = __builtin_bit_cast(float,ordinary);
const float ordinary_float = 1.0f;
static unsigned folded_arithmetic = __builtin_bit_cast(unsigned,ordinary_float + 0.0f);
constexpr unsigned long long shuffled_nan = __builtin_bit_cast(unsigned long long,
    __builtin_shufflevector((F2){__builtin_bit_cast(float,0x7f800001u),1.0f},(F2){0,0},0,1));
static_assert(shuffled_nan == 0x3f8000007f800001ull);

/* Discarded logical operands need not be constant expressions. */
int never_called(void);
constexpr unsigned logical_zero = __builtin_bit_cast(unsigned, 0 && never_called());
constexpr unsigned logical_one = __builtin_bit_cast(unsigned, 1 || never_called());
static_assert(logical_zero == 0 && logical_one == 1);
/* Each intermediate representation is validated once per reader invocation. */
constexpr unsigned nested_casts = __builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,__builtin_bit_cast(unsigned,0x3f800000u))))))))))))))))))))))));
static_assert(nested_casts == 0x3f800000u);
union Active { unsigned u; float f; };
constexpr union Active active = {.u=0x3f800000u};
constexpr unsigned active_bits = __builtin_bit_cast(unsigned,active.u);
static_assert(active_bits == 0x3f800000u);
constexpr V4 indexed_vector = {1,2,3,4};
static_assert(__builtin_bit_cast(unsigned,indexed_vector[2]) == 3);
constexpr unsigned indexed_array[1] = {5};
static_assert(__builtin_bit_cast(unsigned,((const unsigned*)indexed_array)[0]) == 5);

int main(void)
{
	CHECK(__builtin_bit_cast(unsigned,once())==fb && calls==1 && folded_extension==1.0f && folded_arithmetic==fb);
	CHECK(__builtin_bit_cast(unsigned long long,pair())==0x0000000700000003ULL && calls==2);
	volatile unsigned bits=0x80000000u;
	float negative_zero=__builtin_bit_cast(float,bits);
	CHECK(__builtin_bit_cast(unsigned,negative_zero)==bits);
	unsigned char bytes[8]={8,7,6,5,4,3,2,1};
	CHECK(__builtin_bit_cast(unsigned long long,bytes)==0x0102030405060708ULL);
	CHECK(__builtin_bit_cast(Bytes,0x0102030405060708ULL)[2]==6);
	struct Pair p=__builtin_bit_cast(struct Pair,0x0000000900000005ULL);
	CHECK(p.x==5 && p.y==9 && cp.x==3 && cb.b[3]==5 && padded.x==2);
	V4 v={1,2,3,4};
	struct Four { unsigned a,b,c,d; } four=__builtin_bit_cast(struct Four,v);
	CHECK(four.a==1 && four.d==4);
	V4 w=__builtin_bit_cast(V4,four); CHECK(w[1]==2 && w[2]==3);
	struct Fields { unsigned a:3,b:5; } fields = {5,11};
	CHECK(__builtin_bit_cast(unsigned,(unsigned)fields.b)==11);
	int x=5, *ptr=&x;
	CHECK(__builtin_bit_cast(int *,__builtin_bit_cast(unsigned long,ptr))==ptr);
	union Pun u={.u=0x3f800000u}; CHECK(__builtin_bit_cast(float,u)==1.0f);
	struct Large large={}; for(int i=0;i<32;i++) large.x[i]=(unsigned)i;
	typedef unsigned Words[32];
	Words words; for(int i=0;i<32;i++) words[i]=large.x[i];
	struct Large copy=__builtin_bit_cast(struct Large,__builtin_bit_cast(Words,large));
	CHECK(copy.x[31]==31 && __builtin_bit_cast(struct Large,words).x[30]==30);
	/* Copy lvalue bytes, including signaling NaNs, without an FP conversion. */
	union Pun nan={.u=0x7f800001u}; CHECK(__builtin_bit_cast(unsigned,nan.f)==0x7f800001u);
	struct RawLD { unsigned long long lo,hi; } raw;
#if __LDBL_MANT_DIG__ == 64
	raw = (struct RawLD){0x8000000000000001ull,0x7fffull};
#else
	raw = (struct RawLD){1,0x7fff000000000000ull};
#endif
	struct RawLD copied = __builtin_bit_cast(struct RawLD,__builtin_bit_cast(long double,raw));
	CHECK(copied.lo==raw.lo && copied.hi==raw.hi);
	return 0;
}
