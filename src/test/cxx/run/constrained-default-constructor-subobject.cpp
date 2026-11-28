// [over.match.viable]/3, [class.default.ctor]/2: a default constructor
// whose constraints are not satisfied is no subobject's default
// constructor: the enclosing class's implicit one is deleted, not defined
// and ill-formed (libc++'s atomic<T> for a T without T(), as V8's
// CppHeapPointerTableEntry has it).
extern "C" int printf(const char *, ...);
template <class T> struct holder {
    T value;
    holder()
        requires __is_constructible(T)
    = default;
    holder(T v) : value(v) {}
    holder(const holder &) = delete;
};
struct payload {
    int bits;
    payload(int b) : bits(b) {}
};
struct entry {
    holder<payload> slot;
};
struct derived : holder<payload> {};
struct outer {
    entry entries[2];
};
static_assert(!__is_constructible(entry));
static_assert(!__is_constructible(derived));
static_assert(!__is_constructible(outer));
static_assert(__is_constructible(holder<int>));
static_assert(sizeof(entry) == sizeof(int));

int main() {
    outer o{{{{payload(3)}}, {{payload(4)}}}};
    holder<int> plain(5);
    int sum = o.entries[0].slot.value.bits + o.entries[1].slot.value.bits +
              plain.value;
    printf("%d\n", sum);
    return sum - 12;
}
