// [basic.lookup.unqual]/7: the base-clause of a nested class defined outside
// its class finds that class's names, the class's own among them.
template <class T> struct Ref {
    int unref() const { return static_cast<const T *>(this)->d; }
};
template <class T> static int SafeUnref(T *p) { return p->unref(); }

namespace N
{
struct Base {
    int b = 1;
};
struct Y {
    struct Inner;
};
} // namespace N

class W
{
  public:
    int use() { return SafeUnref(rec()); }

  private:
    struct Rec;
    struct Other {
        int o = 2;
    };
    const Rec *rec() const { return fRec; }
    Rec *fRec = nullptr;
    struct Two;

  public:
    void make();
    int two();
};

struct W::Rec : public Ref<Rec> {
    int d = 40;
};
struct W::Two : Other {
};
struct N::Y::Inner : Base {
};

void W::make() { fRec = new Rec; }
int W::two() { return Two().o; }

int main()
{
    W w;
    w.make();
    return w.use() + w.two() + N::Y::Inner().b - 43;
}
