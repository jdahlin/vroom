/* [temp.expl.spec]/1: explicit specializations of a variable template,
   as libc++'s __is_floating_point_impl; inline only if declared so
   ([temp.expl.spec]/12), the primary's specializations inline */
template <class T> inline const bool is_fp = false;
template <> inline const bool is_fp<float> = true;
template <> inline const bool is_fp<double> = true;
static_assert(is_fp<float> && is_fp<double> && !is_fp<int>, "");

template <class T> constexpr int width = sizeof(T) * 8;
template <> constexpr int width<bool> = 1;
static_assert(width<bool> == 1 && width<int> == 32, "");

template <class T> int counter = 0;
template <> int counter<char> = 7;

static const bool *address(const bool &b)
{
    return &b;
}
int main()
{
    /* odr-uses: the variables are emitted */
    if (!*address(is_fp<double>) || *address(is_fp<long>))
        return 1;
    counter<char>++;
    counter<int>++;
    if (counter<char> != 8 || counter<int> != 1)
        return 2;
    return 0;
}
