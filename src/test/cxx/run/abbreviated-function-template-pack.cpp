/* [dcl.fct]/22: "The invented type template-parameter is a template
   parameter pack if the corresponding parameter-declaration declares a
   function parameter pack": libc++'s dangling(auto&&...), a pack after
   declared parameters, a ... in the declarator.  Declared and defined
   here, never used (a use is refused, unsupported/abbreviated-pack-use). */
struct dangling {
    constexpr dangling() noexcept = default;
    constexpr dangling(auto &&...) noexcept {}
};
template <class T> int first(T x, auto &&...) { return x; }
int count(auto... rest) { return sizeof...(rest); }
int arrays(auto (&...x)[3]) { return 0; }
/* a ... after the declarator-id is C's ellipsis: auto x... is no pack */
int c_style(auto x, ...) { return x; }
int main()
{
    return c_style(0);
}
