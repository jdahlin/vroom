/* x & 0 is 0 whatever x is (the kernel's IS_DAX(i) with S_DAX 0): an inlined test folds, the dead call is gone, as clang -O2 */
extern void never(void);
struct inode { unsigned f; };
struct m { struct inode *host; };
static inline int dax(struct m *m) { return m->host && (m->host->f & 0); }
int g;
void f(struct m *m) { if (dax(m)) never(); g = 1; }
int main(void){ struct inode i = {3}; struct m x = { &i }; f(&x); return g != 1; }
