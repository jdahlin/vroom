// Variable templates and their specializations: vcc evaluates each static_assert as true, clang accepts.
template<class T> inline constexpr bool is_int_v = __is_integral(T);
template<class T> constexpr bool is_ptr_v = false;
template<class T> constexpr bool is_ptr_v<T *> = true;
template<class T> constexpr int depth_v = 0;	// (T[N] with N deduced: not yet)
template<class T> constexpr int depth_v<T *> = 1 + depth_v<T>;
template<class T> constexpr int depth_v<const T> = depth_v<T>;
template<class T> constexpr unsigned long size_v = sizeof(T);
template<> constexpr unsigned long size_v<void> = 0;
template<class T, class U> constexpr bool same_v = false;
template<class T> constexpr bool same_v<T, T> = true;
template<class T> struct box { T v; };
template<class T> constexpr bool is_box_v = false;
template<class T> constexpr bool is_box_v<box<T>> = true;
namespace n { template<class T> constexpr bool both_v = is_int_v<T> && !is_ptr_v<T>; }
static_assert(is_int_v<int> && !is_int_v<double>, "");
static_assert(is_ptr_v<int *> && !is_ptr_v<int>, "");
static_assert(depth_v<int> == 0 && depth_v<int *> == 1 && depth_v<int *const *> == 2, "");
static_assert(size_v<int> == 4 && size_v<void> == 0, "");
static_assert(same_v<int, int> && !same_v<int, long>, "");
static_assert(is_box_v<box<int>> && !is_box_v<int>, "");
static_assert(n::both_v<long> && !n::both_v<long *>, "");
template<class T> concept boxed = is_box_v<T>;
template<boxed T> char f(T);
template<class T> requires (!is_box_v<T>) long f(T);
void use(box<int> b, int i) { static_assert(sizeof(f(b)) == 1 && sizeof(f(i)) == 8, ""); }
