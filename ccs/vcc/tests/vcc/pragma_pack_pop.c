/* #pragma pack: pop with a value, pop to a missing label, pop on an empty
   stack, pack(0) and invalid values, all as clang (TODO.md tier 1,
   "#pragma pack(push, ident, N)"). */
#pragma pack(push, 2)
#pragma pack(pop, 1)		/* pop, then 1 */
struct A { char c; int i; };
_Static_assert(sizeof(struct A) == 5, "A");
#pragma pack(push, x, 2)
#pragma pack(push, 4)
#pragma pack(pop, x, 1)
struct B { char c; int i; };
_Static_assert(sizeof(struct B) == 5, "B");
#pragma pack(push, 2)
#pragma pack(pop, nosuch)	/* no such label: nothing popped */
struct C { char c; int i; };
_Static_assert(sizeof(struct C) == 6, "C");
#pragma pack(pop)
#pragma pack(pop)		/* empty stack: unchanged */
struct D { char c; int i; };
_Static_assert(sizeof(struct D) == 5, "D");
#pragma pack(push, y)
#pragma pack(2)
#pragma pack(pop, y)
struct E { char c; int i; };
_Static_assert(sizeof(struct E) == 5, "E");
#pragma pack(4)
#pragma pack(push)
#pragma pack(1)
#pragma pack(pop)
struct F { char c; long l; };
_Static_assert(sizeof(struct F) == 12, "F");
#pragma pack(3)			/* invalid: ignored */
#pragma pack(push, 1, x)	/* malformed: ignored */
struct G { char c; long l; };
_Static_assert(sizeof(struct G) == 12, "G");
#pragma pack(push, 0)		/* 0: the default */
struct H { char c; long l; };
_Static_assert(sizeof(struct H) == 16, "H");
#pragma pack(pop)
#pragma pack(0)
struct I { char c; long l; };
_Static_assert(sizeof(struct I) == 16, "I");

int main(void) { return 0; }
