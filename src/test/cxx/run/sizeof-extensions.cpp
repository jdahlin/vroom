/* clang's and GCC's extensions of [expr.sizeof] and [expr.alignof]:
   [ext:gnu_alignof_expression] alignof and __alignof__ of an expression is
   the alignment of its type ([expr.alignof]/1 takes a type-id);
   [ext:clang_datasizeof] __datasizeof is the size without a class's tail
   padding ([abi:layout]: a POD's is its size, a class derived from one
   reuses nothing of the base's padding) */
struct Pod { int a; char b; };
struct NonPod { NonPod(); int a; char b; };
struct Derived : Pod { char d; };
struct Empty {};
int object;
double doubles[3];
struct Pod pod;
int &reference = object;

static_assert(alignof(object) == 4 && __alignof__(object) == 4);
static_assert(alignof(doubles) == 8 && alignof(doubles[1]) == 8);
static_assert(alignof(pod) == 4 && alignof(pod.b) == 1);
static_assert(alignof(reference) == 4 && alignof(&object) == 8);
static_assert(alignof(1.0f) == 4 && alignof('a') == 1 && alignof(nullptr) == 8);
static_assert(alignof(sizeof(int)) == 8);
static_assert(__is_same(decltype(alignof(object)), unsigned long));
static_assert(__datasizeof(int) == 4 && __datasizeof(Pod) == 8);
static_assert(__datasizeof(NonPod) == 5 && sizeof(NonPod) == 8);
static_assert(__datasizeof(Derived) == 9 && sizeof(Derived) == 12);
static_assert(__datasizeof(Pod[2]) == 16);
static_assert(__datasizeof(pod) == 8 && __datasizeof pod == 8);
static_assert(__datasizeof(Pod &) == 8);
NonPod::NonPod() : a(0), b(0) {}

int main()
{
    return __datasizeof(NonPod) != 5 || alignof(object) != 4;
}
