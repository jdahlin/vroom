// [ext:clang_deduction_guide_attributes] an attribute-specifier-seq before
// a deduction-guide ([temp.deduct.guide]/1), as libc++'s
// _LIBCPP_CTAD_SUPPORTED_FOR_TYPE writes it after a class template:
// a guide whose parameter pack is of a dependent typename.
template <class T = void> struct plus {
    int operator()(int a, int b) const { return a + b; }
};
template <class... Tag>
[[maybe_unused]] plus(typename Tag::allow_ctad...) -> plus<Tag...>;
template <class T> struct holder {
    T value;
};
[[maybe_unused]] holder(int) -> holder<long>;
int main()
{
    plus<> p;
    holder<long> h{3};
    return p(2, 3) == 5 && h.value == 3 ? 0 : 1;
}
