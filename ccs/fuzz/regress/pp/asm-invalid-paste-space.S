#define P(a, b) a ## b
#define Q(a) a ## +
#define R(a) a ## .  x
#define N(x) .y##x
#define W(a, b) P(a, b)
1: P(x, .z) P(x,  .z) Q(x) R(x)
2: W(x, N(.t)) W(x,N(.t)) P(x, N(.t))
3: P(x, .z+) W(x, .z  .w)
4: P(, .z) P( , .z) W(,  .z)
#define V(a, ...) a ## __VA_ARGS__
5: V(x, .q) V(x,  .q, .r)
#define __HYP_CONCAT(a, b)	a ## b
#define HYP_CONCAT(a, b)	__HYP_CONCAT(a, b)
#define HYP_SECTION_NAME(NAME)	.hyp##NAME
#define HYP_SECTION_SYMBOL_NAME(NAME) \
	HYP_CONCAT(__hyp_section_, HYP_SECTION_NAME(NAME))
#define BEGIN_HYP_SECTION(NAME)				\
	HYP_SECTION_NAME(NAME) : {			\
		HYP_SECTION_SYMBOL_NAME(NAME) = .;
A HYP_SECTION_SYMBOL_NAME(.text) B
BEGIN_HYP_SECTION(.text) }
