/* [abi:expressions]: a requires-clause naming a member alias of the class
   template's parameter (std::span's element_type) mangles the parameter */
template <class From, class To>
concept convertible = sizeof(From) == sizeof(To);

template <class T>
struct view {
    using element_type = T;
    int size = 0;
    template <class U>
        requires convertible<const U, element_type>
    view(U)
        : size(sizeof(U))
    {
    }
};

int main()
{
    view<const int> v{1u};
    return v.size - 4;
}
