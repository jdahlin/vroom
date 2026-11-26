/* [dcl.init.ref]/5.3.2: a reference to an enumeration bound to the result
   of a class's conversion function, or not at all (is_constructible) */
enum class color { red, green, blue };
struct holds {
    operator color() const { return color::blue; }
};
struct none {};

static int read(const color &c) { return static_cast<int>(c); }
static int take(color &&c) { return static_cast<int>(c); }

int main()
{
    static_assert(__is_constructible(const color &, holds), "");
    static_assert(__is_constructible(color &&, holds), "");
    static_assert(!__is_constructible(const color &, none), "");
    static_assert(!__is_constructible(color &, holds), "");
    return read(holds()) + take(holds()) - 4;
}
