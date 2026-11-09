/* diag-mode: -c */
/* a call to an error() function that is not folded away is an error, as in
   clang (TODO.md tier 1); the kernel keeps vcc's own rule (-fvcc-kernel) */
extern void bad(void) __attribute__((error("no way")));
extern void bare(void) __attribute__((error("")));
static inline void wrap(int x) { if (x) bad(); }
int f(int x)
{
	if (x)
		bad();
	if (0)
		bad();
	bare();
	wrap(x);
	wrap(x + 1);
	return 0;
}
