// Static data members of reference type ([class.static.data]):
// - static constexpr auto & bound to a function (OpenTTD DEF_CMD_TRAIT pattern)
// - static constexpr int & bound to a static-storage object
// - static inline int & bound to a static-storage object
// - out-of-class defined static int &

int global_a = 10;
int global_b = 20;

int add_one(int x) { return x + 1; }
int times_two(int x) { return x * 2; }

template <int (&F)(int), int &V>
struct Traits {
    static constexpr auto &proc = F;
    static constexpr int &val = V;
};

template <int &V>
struct InlineRef {
    static inline int &val = V;
};

struct OutOfClass {
    static int &ref;
};

int &OutOfClass::ref = global_a;

int main() {
    if (Traits<add_one, global_a>::proc(41) != 42)
        return 1;
    if (Traits<times_two, global_b>::proc(21) != 42)
        return 2;
    if (Traits<add_one, global_a>::val != 10)
        return 3;
    if (&Traits<add_one, global_a>::val != &global_a)
        return 5;
    if (&Traits<add_one, global_a>::proc != &add_one)
        return 10;
    if (&Traits<times_two, global_b>::proc != &times_two)
        return 11;
    OutOfClass::ref = 77;
    if (global_a != 77)
        return 8;
    if (Traits<add_one, global_a>::val != 77)
        return 9;
    return 0;
}
