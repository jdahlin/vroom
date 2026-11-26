// [dcl.typedef]/2, [dcl.type.elab]: a typedef of an elaborated class
// specifier (typedef struct Pool Alias) preserves the class's member scope
// so Alias::Wrap<int> parses as a member template-id.
struct Pool {
    template <typename U>
    struct Wrap {
        U x;
        Wrap(U v) : x(v) {}
    };
};

struct User {
    typedef struct Pool Alias;
    static Alias::Wrap<int> make(int v) { return Alias::Wrap<int>(v); }
};

int main()
{
    auto w = User::make(42);
    return w.x == 42 ? 0 : 1;
}
