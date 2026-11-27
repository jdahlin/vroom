/* [temp.inst]/12: a function template specialization a default argument
   uses is defined where a call uses the default argument
   (SkSL::Block::Make's std::unique_ptr<SymbolTable> = nullptr). */
extern "C" int printf(const char *, ...);

template <class T> struct up {
    T *p;
    template <class U = int> up(decltype(nullptr)) : p(nullptr) {}
    ~up() {}
};
struct ST {};
template <class T> int g(T v) { return v; }

struct Block {
    static int Make(int a, up<ST> s = nullptr, int b = g(2));
};
int Block::Make(int a, up<ST> s, int b) { return a + b + (s.p != nullptr); }

int main()
{
    printf("%d\n", Block::Make(1));
    return 0;
}
