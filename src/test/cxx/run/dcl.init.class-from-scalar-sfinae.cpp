/* [dcl.init.general]/16.6.3, [temp.deduct]/8: copy-initialization of a class
   with no constructor from a non-class value is ill-formed, in a
   substitution a failure (libc++'s is_assignable and
   is_nothrow_assignable). */
template <class T>
T &&value();

template <class T, class U>
concept assignable = requires { value<T>() = value<U>(); };

struct Plain {
    int n;
};

struct Base {};
struct Derived : Base {
    int n;
};

static_assert(!assignable<Plain &, int>);
static_assert(!assignable<Plain &, int *>);
static_assert(!assignable<Derived &, int>);
static_assert(!__is_nothrow_assignable(Plain &, double));
static_assert(!__is_assignable(Plain &, int));
static_assert(assignable<Plain &, Plain>);

int main()
{
    return assignable<Plain &, int> || !assignable<Plain &, const Plain &>;
}
