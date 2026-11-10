/* the kernel's scoped_guard: a for loop whose condition is constant true through a const variable and whose step jumps out */
static const _Bool cls_is_conditional = 0;
int *lockp(void);
int work(void);
#define scoped(lbl) for (int *scope = lockp(); scope || !cls_is_conditional; ({ goto lbl; })) if (0) { lbl: break; } else
int f(void) { scoped(l1) return work(); }
int g(int x) { scoped(l2) { if (x) return 1; } }
