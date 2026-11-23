// [temp.param]/14: a function template's parameter pack followed by a
// parameter with a default (Skia's SkIsFinite), with a fold-expression
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;
template <class T> constexpr bool is_fp = false;
template <> constexpr bool is_fp<float> = true;
template <> constexpr bool is_fp<double> = true;
template <typename T, typename... Pack, enable_if_t<is_fp<T>, bool> = true>
static inline bool IsFinite(T x, Pack... values)
{
    T prod = x - x;
    prod = (prod * ... * values);
    return prod == prod;
}
template <typename T, typename... R, int N = 3>
int count(T, R... r)
{
    return N + 1 + sizeof...(R);
}
int main()
{
    float inf = 1e30f * 1e30f;
    return IsFinite(1.0f, 2.0f, 3.0f) && IsFinite(1.0) &&
                   !IsFinite(1.0f, inf) && count(1) == 4 &&
                   count(1, 'a', 2L) == 6
               ? 0
               : 1;
}
