// enumerations: [dcl.enum], [conv.prom]/4-5, [expr.static.cast]/9-10,
// [expr.prim.id.qual]/4, [stmt.switch]
enum Color { Red, Green = 5, Blue };
enum class Size : unsigned char { Small = 1, Large };
enum Big { BigOne = 0x100000000L, BigTwo };
enum Neg { Minus = -2, Zero = Minus + 2, Plus };
enum : short { Anon = 7 };
typedef enum { TA, TB = 3 } Typed;

struct Holder {
    enum Kind { Empty, Full = 10 };
    Kind kind;
    int value() const { return kind == Full ? 1 : 0; }
};

int by_color(Color c) { return c * 2; }
int by_size(Size s) { return static_cast<int>(s); }
Typed typed(Typed t) { return t; }

int classify(Color c) {
    switch (c) {
    case Red:
        return 1;
    case Green:
        return 2;
    default:
        return 3;
    }
}

int scoped(Size s) {
    switch (s) {
    case Size::Small:
        return 10;
    case Size::Large:
        return 20;
    }
    return 0;
}

int table[Blue];

int main() {
    Color c = Blue;
    Size s = Size::Large;
    int failures = 0;
    failures += c != 6;
    failures += by_color(c) != 12;
    failures += by_size(s) != 2;
    failures += sizeof(Size) != 1;
    failures += sizeof(Big) != 8;
    failures += sizeof(Color) != 4;
    failures += BigTwo != 0x100000001L;
    failures += Plus != 1 || Zero != 0;
    failures += Anon + 1 != 8;
    failures += typed(TB) != 3;
    failures += classify(Red) + classify(Green) + classify(Blue) != 6;
    failures += scoped(Size::Small) + scoped(s) != 30;
    failures += sizeof table != 6 * sizeof(int);
    Holder h;
    h.kind = Holder::Full;
    failures += h.value() != 1;
    failures += Holder::Full != 10;
    Color d = static_cast<Color>(5);
    failures += d != Green;
    Size t = (Size)1;
    failures += t != Size::Small;
    failures += !(s > t);
    unsigned u = Blue;
    failures += u != 6;
    bool b = Red;
    failures += b;
    failures += -Green != -5;
    failures += (c > d ? 0 : 1);
    int arr[3] = {Red, Green, Blue};
    failures += arr[Red] != 0 || arr[2] != 6;
    Color colors[2] = {Green, Blue};
    failures += colors[1] != Blue;
    return failures;
}
