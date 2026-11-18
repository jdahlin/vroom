/* [dcl.fct.def.delete]: a class whose special members are all deleted,
   its destructor too (libc++'s __nat), named as a type but never made */
struct nat {
    nat() = delete;
    nat(const nat &) = delete;
    nat &operator=(const nat &) = delete;
    ~nat() = delete;
};

template <class T> struct pick { enum { value = 1 }; };
template <> struct pick<nat> { enum { value = 2 }; };

int by_pointer(const nat *p) { return p ? 1 : 0; }

struct handle {
    int v;
    handle &operator=(const handle &) = delete;
};

/* [abi:pod]: as clang, a copy assignment operator or destructor deleted on
   its first declaration is no user-declared one: still a POD for the
   purpose of layout, whose tail padding a derived class does not reuse */
struct padded {
    int i;
    char c;
    padded &operator=(const padded &) = delete;
};
struct after_padded : padded {
    char d;
};
static_assert(sizeof(after_padded) == 12, "");
struct user_assigned {
    int i;
    char c;
    user_assigned &operator=(const user_assigned &);
};
struct after_user_assigned : user_assigned {
    char d;
};
static_assert(sizeof(after_user_assigned) == 8, "");

int main()
{
    handle h = {3};
    if (pick<int>::value + pick<nat>::value != 3 || by_pointer(0))
        return 1;
    return h.v == 3 ? 0 : 2;
}
