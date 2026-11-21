// [temp.pre]/1, [basic.link]/3: a static constexpr variable template at
// namespace scope has internal linkage.
template <typename T>
static constexpr bool is_small_v = sizeof(T) <= 4;

static_assert(is_small_v<int>, "");
static_assert(!is_small_v<long long>, "");

int main() {
    return !is_small_v<char> || is_small_v<double>;
}
