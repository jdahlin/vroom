/* [namespace.udecl]/14: a function of the same parameters as a function
   template a using-declaration introduced is another overload, not a
   conflict; the C-linkage declaration joins the set. */
namespace n {
inline float ab(float x) noexcept { return x + 1; }
template <class = int> inline int ab(int x) noexcept { return x; }
} // namespace n
using n::ab;
extern "C" int ab(int x) noexcept { return x + 100; }

int main()
{
    if (ab(1.0f) != 2.0f)
        return 1;
    if (ab(1) != 101)
        return 2;
    return 0;
}
