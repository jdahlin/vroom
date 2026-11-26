/* [dcl.enum]/1,3: opaque-enum-declarations as members, and definitions with
   a qualified enum-head-name */
extern "C" int printf(const char *, ...);

namespace n {
enum class K : int;
}
enum class n::K : int { a = 3, b = a + 4 };

class Blob {
    static constexpr int base = 10;
    enum Pos : unsigned char;
    enum class Q;

  public:
    enum Open : short;
    int run();
};

enum Blob::Pos : unsigned char { p0 = base, p1 };
enum class Blob::Q { x = base * 2, y };
enum Blob::Open : short { o0 = 5, o1 };

int Blob::run()
{
    Pos p = p1;
    return int(p) * 1000 + int(p0) + int(Q::y);
}

int main()
{
    Blob b;
    printf("%d %d %d %d\n", b.run(), int(n::K::b), int(Blob::o1),
           int(Blob::o0));
    return 0;
}
