// [basic.lookup.argdep]/2.3: an enumeration's associated entities include the
// class it is a member of, whose friends are then found (/4.3).
class FP
{
  public:
    enum F : unsigned { a = 1, b = 2 };
    friend F operator|(F x, F y);
    friend int tag(F);
    static F inside() { return a | b; }
};

static FP::F outside() { return FP::a | FP::b; }
static int tagged() { return tag(FP::a); }

FP::F operator|(FP::F x, FP::F y) { return FP::F(unsigned(x) | unsigned(y)); }
int tag(FP::F x) { return x + 40; }

int main() { return FP::inside() + outside() + tagged() - 47; }
