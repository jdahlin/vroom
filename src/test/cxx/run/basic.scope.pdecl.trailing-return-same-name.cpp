// [basic.scope.pdecl]/1: a member function's name is not in scope in its own
// trailing-return-type (even when re-analysing the declarator for its body).
enum align { none, left, right };

struct S {
    align a = left;
    constexpr auto align() const -> align { return a; }
};

int main()
{
    S s;
    return s.align() == left ? 0 : 1;
}
