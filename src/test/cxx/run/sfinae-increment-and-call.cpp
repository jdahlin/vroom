template <class T>
concept can_inc = requires(T x) { ++x; };

template <class F, class X>
concept can_call = requires(F f, X x) { f(x); };

struct plain {};
enum color { red };
struct functor {
    template <class T>
    auto operator()(T x) -> decltype(x + 1) {
        return x + 1;
    }
};

int main() {
    if (can_inc<plain> || can_inc<color> || !can_inc<int>)
        return 1;
    if (can_call<functor, plain> || !can_call<functor, int>)
        return 2;
    return 0;
}
