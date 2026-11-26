/* [class.access]/4, /6: access is of the name used, from the declared
   entity */
extern "C" int printf(const char *, ...);
template <class Traits> int find(int k) { return k == Traits::GetKey(k); }
class Owner {
    struct Tr {
        static int GetKey(int v) { return v; }
    };

public:
    int f() { return find<Tr>(3); }
};
class Clip {
    class Builder {
        int x = 5;
        class Blitter;

    public:
        int run();
    };
    class Layer {
        int v;

    public:
        Layer(int);
        ~Layer();
        operator int();
    };

public:
    static int go();
};
struct Base {};
class Clip::Builder::Blitter final : public Base {
public:
    int g(Builder *b) { return b->x; }
};
int Clip::Builder::run()
{
    Blitter b;
    return b.g(this);
}
Clip::Layer::Layer(int a) : v(a) {}
Clip::Layer::~Layer() {}
Clip::Layer::operator int() { return v; }
int Clip::go()
{
    Builder b;
    Layer l(7);
    return b.run() * 10 + l;
}
int main()
{
    Owner o;
    printf("%d %d\n", o.f(), Clip::go());
    return !(o.f() == 1 && Clip::go() == 57);
}
