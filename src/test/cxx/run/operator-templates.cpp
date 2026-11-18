/* [temp.over]/1 for operator expressions ([over.match.oper]/3): operator
   function templates are candidates, their specializations deduced from
   the operands, as libc++'s std::byte shifts */
enum class byte : unsigned char {};
template <class I> constexpr byte &operator<<=(byte &l, I s) noexcept
{
    return l = static_cast<byte>(
               static_cast<unsigned char>(static_cast<unsigned int>(l) << s));
}
template <class I> constexpr byte operator<<(byte l, I s) noexcept
{
    return static_cast<byte>(
        static_cast<unsigned char>(static_cast<unsigned int>(l) << s));
}
struct V {
    int v;
};
template <class T> bool operator==(const V &a, const T &b)
{
    return a.v == b;
}
int main()
{
    byte b = static_cast<byte>(1);
    b <<= 2;
    byte c = b << 1;
    long shift = 1;
    byte d = c << shift;
    V v = {8};
    if ((int)c != 8 || (int)d != 16)
        return 1;
    if (!(v == 8) || v == 9L)
        return 2;
    return 0;
}
