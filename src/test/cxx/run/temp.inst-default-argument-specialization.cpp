/* [temp.inst]/12: a constructor template specialization a default argument
   uses is defined where a call uses the default argument
   (SkSL::Block::Make's std::unique_ptr<SymbolTable> = nullptr). */
extern "C" int printf(const char *, ...);

template <class T> struct up {
    T *p;
    template <class U = int> up(decltype(nullptr)) : p(nullptr) {}
    ~up() {}
};
struct ST {};

struct Block {
    static int Make(int a, up<ST> s = nullptr);
};
int Block::Make(int a, up<ST> s) { return a + (s.p != nullptr); }

int main()
{
    printf("%d\n", Block::Make(1));
    return 0;
}
