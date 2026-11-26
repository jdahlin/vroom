/* [abi:expressions] tl <type> E for ns::S<T>{} in a return type, S<T> a
   dependent specialization (json's identity_tag<TargetType>{}) */
extern "C" int printf(const char *, ...);

namespace d
{
template <class T> struct tag {
};
} // namespace d

namespace n
{
template <class J> int get(J j, d::tag<int>)
{
    return j + 1;
}
} // namespace n

template <class J, class T = int>
auto from(J j) -> decltype(n::get(j, d::tag<T>{}))
{
    return n::get(j, d::tag<T>{});
}

int main()
{
    printf("%d\n", from(4));
    return 0;
}
