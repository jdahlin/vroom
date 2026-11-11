/* vcc-flags: -std=c23 */
/* C23 constant objects preserve values through aggregates, array subscripts,
   zero initialization and explicitly rounded conversions. */
constexpr int count = 4;
constexpr double half = 0.5;
constexpr float rounded = (float)0.1;
constexpr unsigned wrapped = (unsigned)-1;
constexpr int *null_pointer = 0;
constexpr volatile int *null_volatile_pointer = 0;
struct Inner { int value; };
struct Object { int first; struct Inner inner; int values[4]; double fraction; };
constexpr struct Object object = { 3, { 6 }, { 2, 4, 8, 16 }, 0.5 };
constexpr struct Object zero = {};
constexpr struct Object copy = object;
constexpr int nested = object.inner.value;
constexpr int middle = object.values[2];
constexpr char word[] = "cat";
constexpr int letter = word[1];
constexpr auto inferred = 17;
constexpr auto inferred_half = 0.5;
constexpr _Complex float complex_small = 0.5f + 0.25fi;
constexpr _Complex double complex_wide = complex_small;
enum { enum_count = count, enum_nested = object.inner.value };
_Static_assert(count == 4 && enum_count == 4);
_Static_assert(enum_nested == 6 && nested == 6);
_Static_assert(object.values[2] == 8 && middle == 8);
_Static_assert(zero.inner.value == 0 && zero.values[3] == 0);
_Static_assert(letter == 'a');
_Static_assert(copy.first == 3 && copy.inner.value == 6);
_Static_assert(_Generic(&null_pointer, int *const *: 1, default: 0));
_Static_assert(_Generic(&null_volatile_pointer, volatile int *const *: 1, default: 0));
_Static_assert(inferred == 17);
int main(void)
{
    constexpr int local = nested + middle;
    constexpr struct Inner local_object = { local };
    constexpr int local_member = local_object.value;
    int values[count];
    for (int i = 0; i < count; i++) values[i] = local_member + i;
    if (values[3] != 17 || local_member != 14 || half != 0.5 || inferred_half != 0.5)
        return 1;
    if (null_pointer || null_volatile_pointer || wrapped != (unsigned)-1)
        return 2;
    if (__real__ complex_wide != 0.5 || __imag__ complex_wide != 0.25)
        return 4;
    if (rounded != (float)0.1 || word[2] != 't' || letter != 'a')
        return 3;
    return 0;
}
