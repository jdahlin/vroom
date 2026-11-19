/* [class.static.data]/3, [dcl.constexpr]/1, [dcl.inline]/6: a const static
   data member of literal type declared in its class, defined out of the
   class with inline constexpr or constexpr: an inline variable, emitted
   COMDAT as clang.  libc++'s ordering.h: partial_ordering::less. */
struct ordering {
    int value;
    constexpr ordering(int v) : value(v) {}
    static const ordering less;
    static const ordering greater;
};
inline constexpr ordering ordering::less(-1);
constexpr ordering ordering::greater{1};

static int get(const ordering *o) { return o->value; }

int main(void)
{
    if (get(&ordering::less) != -1 || get(&ordering::greater) != 1)
        return 1;
    return 0;
}
