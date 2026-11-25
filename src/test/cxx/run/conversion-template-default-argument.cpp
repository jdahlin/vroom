// a conversion function template whose second template parameter has a default template-argument (json's
// operator ValueType with enable_if): deduced from the target type, the default substituted ([temp.deduct.conv])
template<bool B, class T = void> struct en {};
template<class T> struct en<true, T> { using type = T; };
template<class T, class U> struct same { static constexpr bool value = false; };
template<class T> struct same<T, T> { static constexpr bool value = true; };
struct J {
    template<class V, typename en<!same<V, J>::value, int>::type = 0>
    operator V() const { return V(3); }
};
int main() {
    J j;
    int a = static_cast<int>(j);
    int b = j;
    return a + b - 6;
}
