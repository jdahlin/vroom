/* [dcl.constexpr]/10, [dcl.init]/7.1: a constexpr object of an empty class
   default-initialized by its trivial default constructor (libc++'s
   std::ignore) */
struct ignore_type {
    int get(int n) const { return n + 6; }
};
inline constexpr ignore_type ignore;
constexpr ignore_type other;

int main(int argc, char **)
{
    return ignore.get(argc) + other.get(argc) == 14 ? 0 : 1;
}
