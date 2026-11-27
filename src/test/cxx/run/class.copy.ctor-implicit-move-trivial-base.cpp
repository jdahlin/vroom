/* [class.copy.ctor]/8: a class whose base only moves, by a defaulted
   trivial move constructor, has an implicit move constructor. */
struct move_only {
    move_only() = default;
    move_only(move_only &&) = default;
    move_only(const move_only &) = delete;
};
struct derived : move_only {
    int v = 7;
};
struct member {
    move_only m;
    int v = 3;
};
/* (a virtual destructor from a base is no user-declared one) */
struct frame : move_only {
    frame() = default;
    frame(frame &&) = default;
    virtual ~frame() {}
    int v = 1;
};
struct key_frame : frame {};
static_assert(__is_constructible(key_frame, key_frame &&));
static_assert(__is_constructible(derived, derived &&));
static_assert(__is_constructible(member, member &&));
static_assert(!__is_constructible(derived, derived &));
derived pass(derived &d) { return static_cast<derived &&>(d); }
member pass(member &d) { return static_cast<member &&>(d); }
int main()
{
    derived d;
    member m;
    key_frame k;
    key_frame moved(static_cast<key_frame &&>(k));
    return pass(d).v + pass(m).v + moved.v - 11;
}
