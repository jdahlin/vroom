/* a constant from a folded call decides an if (the kernel's vruntime_cmp: !__builtin_strcmp(op, "<")): the dead chain's never() is gone, as clang -O2 */
extern void never(void);
#define CMP(A, S, B) ({ int r_ = 0; if (!__builtin_strcmp(S, "<")) r_ = (A) < (B); else if (!__builtin_strcmp(S, ">")) r_ = (A) > (B); else never(); r_; })
int main(void) { return !(CMP(1, "<", 2) && CMP(3, ">", 2)); }
