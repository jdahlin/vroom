// [class.access], [class.default.ctor]/2: a defaulted constructor keeps
// its access and a friend's derived constructor may initialize its base.
struct Base {
    int value = 17;
private:
    Base() = default;
    friend struct Friend;
};
struct Friend : Base { Friend() {} };
struct Protected {
    int value = 23;
protected:
    Protected() = default;
};
struct Derived : Protected { Derived() {} };
static_assert(!__is_constructible(Base));
static_assert(!__is_constructible(Protected));
static_assert(__is_constructible(Friend));
static_assert(__is_constructible(Derived));
int main() {
    Friend f;
    Derived d;
    return f.value != 17 || d.value != 23;
}
