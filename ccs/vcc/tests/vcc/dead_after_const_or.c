/* if (a || !always_false()) return 0; with the inlined operand constant true the
   condition never reaches the code after the if (the kernel's
   folio_unqueue_deferred_split without THP: a call to a function no one
   defines).  And IS_DAX(inode) with S_DAX 0: x & 0 decides the if.  The
   never_defined_* calls would make the link fail. */
#include <stdio.h>

struct folio { unsigned long flags, f1; void *next, *prev; };
struct inode { unsigned i_flags; };
_Bool never_defined_split(struct folio *f);
int never_defined_dax(struct inode *i);
static inline _Bool test_large(const struct folio *f) { return f->flags & 64; }
static inline unsigned order(const struct folio *f) { if (!test_large(f)) return 0; return f->f1 & 0xff; }
static inline _Bool test_rmappable(const struct folio *f) { (void)f; return 0; }
static inline _Bool unqueue(struct folio *f)
{
	if (order(f) <= 1 || !test_rmappable(f))
		return 0;
	if (f->next == f->prev)
		return 0;
	return never_defined_split(f);
}
#define S_DAX 0
#define IS_DAX(inode) ((inode)->i_flags & S_DAX)
static int dedupe(struct inode *in, int flags, void *ops)
{
	int ret = -1;

	if (flags & 4) {
		if (!IS_DAX(in))
			ret = (int)in->i_flags;
		else if (ops)
			ret = never_defined_dax(in);
	}
	return ret;
}

int main(void)
{
	struct folio f = { 64, 5, &f, 0 };
	struct inode in = { 9 };

	printf("%d %d %d\n", unqueue(&f), dedupe(&in, 4, &f), dedupe(&in, 0, 0));
	return 0;
}
