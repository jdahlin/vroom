/* vcc-flags: -std=c23 */
/* vcc-threads-same */
/* VCC_THREADS=6 exercises concurrent constant evaluation during emission. */
struct Leaf { int value; };
struct Object { int x; double f; int a[3]; struct Leaf leaf; int *pointer; };
constexpr struct Object zero = {};
constexpr struct Object copy = zero;
constexpr char word[] = "abc";
const int first = 5;
const int second = first + 1;
#define ONE(N) int value_##N = zero.x + copy.leaf.value + zero.a[2] + word[1] - 'b' + second + 11;
#define TEN(N) ONE(N##a) ONE(N##b) ONE(N##c) ONE(N##d) ONE(N##e) ONE(N##f) ONE(N##g) ONE(N##h) ONE(N##i) ONE(N##j)
#define HUNDRED(N) TEN(N##a) TEN(N##b) TEN(N##c) TEN(N##d) TEN(N##e) TEN(N##f) TEN(N##g) TEN(N##h) TEN(N##i) TEN(N##j)
HUNDRED(a) HUNDRED(b) HUNDRED(c) HUNDRED(d) HUNDRED(e)
#undef ONE
#define ONE(N) &value_##N,
int *values[] = { HUNDRED(a) HUNDRED(b) HUNDRED(c) HUNDRED(d) HUNDRED(e) };
double floating_zero = zero.f;
int *null_pointer = copy.pointer;
_Static_assert(zero.a[1] == 0 && copy.leaf.value == 0, "implicit nested zeros");
_Static_assert(word[2] == 'c', "string subobject");
int main(void)
{
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        if (*values[i] != 17) return 1;
    return floating_zero != 0 || null_pointer != 0;
}
