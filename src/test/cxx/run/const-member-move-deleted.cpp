/* [class.copy.ctor]/10, [over.ics.ref]/3: a const member of a move-only class
   type is copied by X(X&&), so the implicit copy and move constructors are
   deleted; the class still compiles and is used otherwise. */
struct move_only {
    move_only() {}
    move_only(const move_only &) = delete;
    move_only(move_only &&) {}
};

class holder {
public:
    holder() : value(1) {}
    int get() const { return value; }
    const move_only token;
    int value;
};


int main()
{
    holder h;
    return h.get() - 1;
}
