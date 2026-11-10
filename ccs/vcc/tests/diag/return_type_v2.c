/* diag-differs: body_falls_off warns after a switch on an enum that returns in every case (TODO.md: "Merged diagnostics: cases from the D8 branch") */
/* D5.1: -Wreturn-type as clang's CFG decides it: constant conditions pruned
   (also "x || 1" and a const variable), noreturn calls and builtins end a path,
   a label is reached only by a reachable goto, a switch on an enum covering every
   enumerator has no way past it.  No false positive matters most: the kernel
   builds with -Werror=return-type. */
_Noreturn void die(void);
enum e { A, B };
static const _Bool no = 0;
int gp(void);

int some(int x) { if (x) return 1; }
int none(int x) { x++; }
int loop(int x) { while (1) { if (x) return 1; } }
int brk(int x) { for (;;) { if (x) break; } }
int sw(int x) { switch (x) { case 1: return 1; } }
int en(enum e v) { switch (v) { case A: return 1; case B: return 2; } }
int nr(int x) { if (x) return 1; die(); }
int dw(int x) { do { __builtin_unreachable(); } while (0); }
int guard(void) { for (int s = 0; gp() || !no; ({ goto out; })) if (0) { out: break; } else return 1; }
int jump(int x) { if (x) goto a; return 1; a: ; }
int main(void) { }
