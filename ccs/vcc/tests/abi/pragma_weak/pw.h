/* #pragma weak across compilers: the callee's weak definitions lose to the
   caller's strong ones at link time; the weak aliases resolve to their
   targets.  A strong definition of the same name in both halves would not
   link, so the bindings are checked by the linker. */
int wdef(void);		/* weak in callee.c, strong in caller.c */
int wonly(void);	/* weak in callee.c only */
extern int wvar;	/* weak in callee.c, strong in caller.c */
extern int wvar2;	/* weak in callee.c only */
int walias(void);	/* weak alias of impl (callee.c) */
int wover(void);	/* weak alias in callee.c, strong in caller.c */
int wlater(void);	/* alias pragma before its target is declared */
extern int valias;	/* weak alias of a variable */
int callee_aliases(void);	/* the aliases used in callee.c: 1 */
/* Darwin: clang makes #pragma weak aliases local symbols (.weak_reference
   on a definition), so only their own TU sees them there */
