/* [ext:gnu_attributes] always_inline and noinline on an explicit
   specialization of a function template are its own (Highway's
   HWY_INLINE) */
template <class T> constexpr bool is_int() { return false; }
template <>
inline __attribute__((always_inline)) constexpr bool is_int<int>()
{
    return true;
}
template <class T> int size(T) { return 1; }
template <> inline __attribute__((noinline)) int size<long>(long) { return 8; }

static_assert(is_int<int>() && !is_int<char>());

int main() { return !is_int<int>() || size(1L) + size('a') != 9; }
