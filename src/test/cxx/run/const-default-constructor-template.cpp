// [dcl.init]/7: a constructor template with a parameter pack makes the
// class const-default-constructible.
struct NoDestructor {
    template <typename... A> explicit NoDestructor(A &&...) : v(sizeof...(A) + 5)
    {
    }
    int v;
};
static const NoDestructor global;
int main()
{
    static const NoDestructor local;
    return global.v + local.v - 10;
}
