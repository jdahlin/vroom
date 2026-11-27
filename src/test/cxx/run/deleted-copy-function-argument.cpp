/* [over.best.ics]/4: a function argument reaches a deleted implicit copy
   constructor only by a user-defined conversion: no candidate */
extern "C" int printf(const char *, ...);
typedef int (*factory)(unsigned long);
struct nc { nc() {} nc(const nc &) = delete; };
struct cache {
    typedef factory F;
    cache(F f) : v(f(3)) {}
    explicit cache(unsigned long n) : v((int)n) {}
    virtual ~cache() {}
    int v;
    nc n;
};
static int make(unsigned long b) { return (int)b * 7; }
static int (*gp)(unsigned long) = make;
int main() { cache c(make); cache d(gp); cache e(&make); printf("%d %d %d\n", c.v, d.v, e.v); }
