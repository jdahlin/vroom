// [except.spec]: bare and conditional specifiers; [conv.fctptr] conversion
// to a potentially-throwing function pointer; [abi:mangle.function-type] Do.
static constexpr int enabled = 2;
typedef int (*NonThrowing)(int) noexcept;
typedef int (*PotentiallyThrowing)(int);

static int consume(const int*, int) noexcept;
static int consume(const int*, int n) noexcept(enabled > 1) { return n; }

int fast(int n) noexcept { return consume(nullptr, n) + 1; }
int slow(int n) noexcept(false) { return n + 2; }

int target(int n) noexcept { return fast(n); }
int target(long n) { return n + 10; }

int pick(NonThrowing p) { return p(2); }
int pick(PotentiallyThrowing p) { return p(4); }

int apply(PotentiallyThrowing p) { return p(1); }

struct S {
    int get() const noexcept(1) { return 5; }
};

int main()
{
    NonThrowing exact = &fast;
    PotentiallyThrowing converted = target;
    PotentiallyThrowing copied = exact;
    PotentiallyThrowing throwing = &slow;
    S s;
    return pick(fast) + pick(throwing) + apply(fast) + converted(0) +
               copied(0) + s.get() != 18;
}
