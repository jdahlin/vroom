// friend class P; with no P yet declares P in the enclosing namespace, not in G:
// P::H in G's members is the later ::P's (Skia: SkRegion befriends SkRegionPriv)
class G {
public:
    void f();
private:
    friend class P;
    struct H;
    H *h;
};
class P {
public:
    typedef G::H H;
};
struct G::H { int x; };
void G::f() { h = ((P::H *)-1); }
