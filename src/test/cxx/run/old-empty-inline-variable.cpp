// an inline variable of an empty class initialized by T() (libc++'s std::piecewise_construct) is defined
// here, zeroed: before, only declared (weak), so &piecewise_construct was null and map::operator[] crashed
// std: c++17
struct pc_t { explicit pc_t() = default; };
inline constexpr pc_t pc = pc_t();
__attribute__((noinline)) static int take(const pc_t &p) { return &p != nullptr; }
template <class... A> __attribute__((noinline)) int fwd(A &&...a) { return take(static_cast<A &&>(a)...); }
int main() { return take(pc) && fwd(pc) ? 0 : 1; }
