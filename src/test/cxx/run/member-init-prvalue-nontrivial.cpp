/* [class.base.init]/7, [dcl.init]/17.6.1, [class.copy.elision]/1: a member
   of a class with non-trivial copying initialized from a prvalue of its
   class is that object: no copy or move constructor runs. */
static int constructed;
static int copied;
static int destroyed;

struct Info {
    int w, h;
    Info(int w_, int h_) : w(w_), h(h_) { constructed++; }
    Info(const Info &o) : w(o.w), h(o.h) { copied++; }
    ~Info() { destroyed++; }
    static Info make(int w, int h) { return Info(w, h); }
};

struct Holder {
    int *p;
    Info info;
    Holder() : p(nullptr), info(Info::make(3, 4)) {}
    explicit Holder(int n) : p(nullptr), info(Info(n, n + 1)) {}
};

int main() {
    {
        Holder h;
        if (h.info.w != 3 || h.info.h != 4)
            return 1;
        Holder g(7);
        if (g.info.w != 7 || g.info.h != 8)
            return 2;
    }
    if (copied != 0)
        return 3;
    if (constructed != 2)
        return 4;
    if (destroyed != 2)
        return 5;
    return 0;
}
