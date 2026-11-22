// [class.friend]/1: a member function, a constructor and a destructor of
// another class as friends; each reaches the private members.
struct X;
struct Y {
    Y();
    ~Y();
    int get(const X &x);
    static int sget(const X &x);
    int total = 0;
};

class X {
    int secret = 7;
    friend int Y::get(const X &);
    friend int Y::sget(const X &);
    friend Y::Y();
    friend Y::~Y();
    static int count;
};

int X::count = 0;
Y::Y() { X x; total = x.secret; X::count++; }
Y::~Y() { X x; total += x.secret; X::count += 10; }
int Y::get(const X &x) { return x.secret; }
int Y::sget(const X &x) { return x.secret + 1; }

int main()
{
    X x;
    int r = 0;
    {
        Y y;
        if (y.total != 7)
            return 1;
        r = y.get(x) + Y::sget(x);
    }
    return r == 15 ? 0 : 2;
}
