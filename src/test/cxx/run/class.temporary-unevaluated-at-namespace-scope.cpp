// [expr.context]/1: a temporary of a call's argument in an unevaluated operand
// at namespace scope has no storage ([class.temporary])
struct Pod {};
struct Value {
    int v;
};

void quiet(const Pod &) noexcept;
void loud(const Pod &);
void take(Value &&) noexcept;
int size_of(const Value &);

static_assert(noexcept(quiet(Pod{})), "");
static_assert(!noexcept(loud(Pod{})), "");
static_assert(noexcept(take(Value{1})), "");
static_assert(noexcept(take(Value())), "");
static_assert(sizeof(size_of(Value{2})) == sizeof(int), "");
decltype(size_of(Value{3})) zero = 0;

int main()
{
    return zero;
}
