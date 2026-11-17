// [class.default.ctor]/4, [class.copy.ctor]/14, [class.dtor]/12: the
// implicitly-defined default constructor, copy constructor and destructor
// of classes whose bases or members have user-provided ones
extern "C" int printf(const char *, ...);

int events[64];
int count;
void note(int e) { events[count++] = e; }

struct R {
    int id;
    R() : id(1) { note(1); }
    R(const R &o) : id(o.id + 10) { note(2); }
    ~R() { note(3 + id); }
};
struct Holder {
    int x;
    R r;
};
struct Derived : R {
    char tag;
};
struct Both : Derived {
    Holder h;
    int y;
};
struct OnlyDtor {
    ~OnlyDtor() { note(50); }
};
struct HasOnlyDtor {
    OnlyDtor d;
    int v;
};


int main() {
    int t = 0;
    {
        Holder h;
        h.x = 4;
        Holder copy = h;
        t += copy.r.id + copy.x + h.r.id;
    }
    {
        Derived d;
        d.tag = 2;
        Derived e(d);
        t += e.id + e.tag;
    }
    {
        Both b;
        b.y = 3;
        Both c = b;
        t += c.h.r.id + c.id + c.y;
    }
    {
        HasOnlyDtor o;
        o.v = 7;
        t += o.v;
    }
    for (int i = 0; i < count; i++)
        t = (t * 3 + events[i]) % 100003;
    printf("%d %d\n", t, count);
    return 0;
}
