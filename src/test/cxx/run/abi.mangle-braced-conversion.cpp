/* [abi:expressions] R{} of a class in a return type's decltype:
   tl <type> E */
struct R {
    int v = 3;
};
template <class T> T val();
template <class L> decltype((void)(val<L>() == 1), R{}) none(L)
{
    return R{};
}
int main()
{
    return !(none(1).v == 3);
}
