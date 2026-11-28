/* [stmt.switch]/2, [conv.general]/5: a switch condition of class type is
   converted by its conversion functions to one integral or enumeration
   type */
enum class status : unsigned char { init, busy, done };
struct number {
    int n;
    operator int() const { return n; }
    explicit operator long() const { return 0; }
};
/* as std::atomic's two conversions to one type */
template <class T> struct atomic {
    T value;
    operator T() const volatile { return value; }
    operator T() const { return value; }
};
static int classify(status s_)
{
    atomic<status> s{s_};
    switch (s) {
    case status::init:
        return 1;
    case status::busy:
        return 2;
    default:
        return 3;
    }
}
int main()
{
    if (classify(status::init) != 1 || classify(status::done) != 3)
        return 1;
    number n{4};
    switch (n) {
    case 4:
        break;
    default:
        return 2;
    }
    switch (number c = {7}; number d = {c.n + 1}) {
    case 8:
        return 0;
    }
    return 3;
}
