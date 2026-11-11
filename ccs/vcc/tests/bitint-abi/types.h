/* Transport probe. ABI_PROXY deliberately replaces _BitInt with an AAPCS
 * aggregate only for the Clang ARM half: Clang23 rejects ARM widths >128.
 * That run checks our documented indirect ABI, not Clang _BitInt parity. */
#ifndef WIDTH
#define WIDTH 500
#endif
#ifndef TYPE_ALIGN
#define TYPE_ALIGN 8
#endif
#ifdef ABI_PROXY
typedef struct { unsigned long long limb[(WIDTH+63)/64]; } Big __attribute__((aligned(TYPE_ALIGN)));
#elif SIGNED
typedef _BitInt(WIDTH) Big __attribute__((aligned(TYPE_ALIGN)));
#else
typedef unsigned _BitInt(WIDTH) Big __attribute__((aligned(TYPE_ALIGN)));
#endif
_Static_assert(sizeof(Big)==8*((WIDTH+63)/64),"limb storage");
_Static_assert(_Alignof(Big)==TYPE_ALIGN,"Linux alignment");
typedef struct { char lead; Big value; char tail; } Wrapped;
_Static_assert(sizeof(Wrapped)==((TYPE_ALIGN+sizeof(Big)+TYPE_ALIGN)&~(TYPE_ALIGN-1)),"record layout");
Big relay(Big,long,Big);
Big pressure(long,long,long,long,long,long,long,long,Big,long);
Big variadic(int,...);
Wrapped wrap(Wrapped);
int parameter_alignment(Big);
Big variadic_stack(long,long,long,long,long,long,long,long,...);
