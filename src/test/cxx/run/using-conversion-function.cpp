// [namespace.udecl]: a using-declaration of a base's conversion function
// makes it accessible in the derived class.
struct Writer {
    explicit operator bool() const { return p != nullptr; }
    operator int() const { return 7; }
    void *p = nullptr;
    int mark() const { return 3; }
};

struct Vertex : private Writer {
    Vertex(void *q) { p = q; }
    using Writer::mark;
    using Writer::operator bool;
    using Writer::operator int;
};

int main()
{
    int x;
    Vertex a(&x), b(nullptr);
    int r = 0;
    if (a)
        r += 1;
    if (!b)
        r += 2;
    int i = a;
    return r + i + a.mark() - 13;
}
