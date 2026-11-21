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

int check_no_move();

int main()
{
    handle h = {3};
    if (pick<int>::value + pick<nat>::value != 3 || by_pointer(0))
        return 1;
    return h.v == 3 && check_no_move() == 26 ? 0 : 2;
}

/* [dcl.fct.def.delete] a deleted move constructor is considered only
   when selected. A copy from an lvalue and a prvalue return are valid. */
struct no_move {
    int value;
    no_move(int n) : value(n) {}
    no_move(const no_move &other) : value(other.value) {}
    no_move(no_move &&) = delete;
};
no_move make_no_move() { return no_move(17); }
int check_no_move() {
    no_move a(9);
    no_move b(a);
    no_move c = make_no_move();
    return b.value + c.value;
}
