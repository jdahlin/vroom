/* D5.3: a failed static assertion names the requirement that failed (the false
   operand of a chain of &&, as clang), shows its operands' values, and for
   sizeof(struct ...) the record's layout: where the padding is, and the size in
   decreasing alignment order */
/* diag-differs: the layout notes are vcc's (clang has none) */
struct hdr {
	char tag;
	long len;
	short kind;
};
typedef struct { int a; char b; } pair;

_Static_assert(sizeof(struct hdr) == 16, "hdr must be 16 bytes");
_Static_assert(sizeof(pair) <= 4 && 1, "pair");
_Static_assert(1 > 2 && 2 > 1, "logic");
_Static_assert(0, "zero");
