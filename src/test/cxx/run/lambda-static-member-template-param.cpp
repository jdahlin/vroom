// Lambda in a static data member initializer of a class template sees the
// enclosing class's scope (template parameters and members).

template <class State, State LockedBit>
struct atomic_lock {
    static constexpr State mask = LockedBit * 2;
    static constexpr State applied = [](State s) { return s | LockedBit; }(1u);
    static constexpr State applied_both = [](State s) {
        return s | LockedBit | mask;
    }(1u);
};

int main() {
    if (atomic_lock<unsigned, 4>::applied != 5u)
        return 1;
    if (atomic_lock<unsigned, 4>::applied_both != 13u)
        return 2;
    if (atomic_lock<unsigned, 8>::applied != 9u)
        return 3;
    if (atomic_lock<unsigned, 8>::applied_both != 25u)
        return 4;
    return 0;
}
