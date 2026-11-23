/* [over.match.funcs]/2, /4: one name for a static and a non-static member function: the qualified call without an object reaches the static one (the non-static takes no such arguments), the call on an object the non-static (syntax_child::node in parse.h) */
struct child {
    unsigned bits;
    static child node(unsigned n) { return {n << 1}; }
    static child token(unsigned t) { return {t << 1 | 1}; }
    unsigned node() const { return bits >> 1; }
    bool is_token() const { return bits & 1; }
};
int main()
{
    child a = child::node(5);
    child b = child::token(3);
    return a.node() - 5 + b.node() - 3 + a.is_token() + !b.is_token();
}
