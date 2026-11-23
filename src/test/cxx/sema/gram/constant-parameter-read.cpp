// [expr.const]/5.8, /5.12: a function's parameters hold the caller's values:
// a pointer parameter's value and a reference parameter name are no constant
// expression (static_assert is ill-formed), a constexpr local's are; the lines
// after each are their well-formed near misses.
struct node {
    int value;
    node *next;
};
void pointer_read(node *p)
{
    constexpr int v = p->value;
}
void pointer_compare(node *p)
{
    constexpr bool v = p == nullptr;
}
void pointer_null_test(node *p)
{
    constexpr bool v = !p;
}
void reference_member(node &r)
{
    constexpr int v = r.value;
}
void reference_read(int &r)
{
    constexpr int v = r;
}
void rvalue_reference_read(int &&r)
{
    constexpr int v = r;
}
void constant_pointer_size(node *p)
{
    static_assert(sizeof(*p) == sizeof(node));
    static_assert(sizeof(p->next) == sizeof(void *));
    static_assert(noexcept(p->value));
}
void constant_reference_size(node &r)
{
    static_assert(sizeof(r) == sizeof(node));
    static_assert(sizeof(r.value) == sizeof(int));
}
