/* a variable template declared after attributes without effect
   (libc++'s _LIBCPP_DEPRECATED_IN_CXX20 is_pod_v) */
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
template <class T>
__attribute__((__deprecated__)) inline constexpr bool four_v = sizeof(T) == 4;
template <class T> [[deprecated]] constexpr int size_v = sizeof(T);

static_assert(four_v<int>, "");
static_assert(!four_v<char>, "");

int main() { return size_v<long> == 8 && four_v<float> ? 0 : 1; }
