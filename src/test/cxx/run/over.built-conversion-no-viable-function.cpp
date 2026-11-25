/* cxx-run-flags: -std=c++17 */
/* [over.match.oper]/3: operator functions of the name none of which is
   viable leave the built-in comparison of a converted class operand */
struct L {
    int v;
    operator int() const
    {
        return v;
    }
};
struct Other {};
bool operator==(const Other &, const Other &)
{
    return false;
}
bool operator<(const Other &, const Other &)
{
    return false;
}
int main()
{
    L l{5};
    return !(5 == l && l == 5 && 4 < l && l + 1 == 6);
}
