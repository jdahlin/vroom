/* Long flat declarators and label sequences must not consume parser stack. */
#define TEN(x) x x x x x x x x x x
#define HUNDRED(x) TEN(TEN(x))
#define THOUSAND(x) TEN(HUNDRED(x))
#define TEN_THOUSAND(x) TEN(THOUSAND(x))
typedef int TEN_THOUSAND(*) Deep;
Deep deep;
typedef char Array100 HUNDRED([1]);
_Static_assert(sizeof(Array100) == 1, "many array suffixes");
typedef int TEN_THOUSAND(*) (*DeepFn)(void);
DeepFn callback;
_Static_assert(sizeof deep == sizeof(void *), "deep pointer size");
_Static_assert(__builtin_types_compatible_p(__typeof__(callback()), Deep), "grouped function pointer");

#define C10(x) case x##0: case x##1: case x##2: case x##3: case x##4: case x##5: case x##6: case x##7: case x##8: case x##9:
#define C100(x) C10(x##0) C10(x##1) C10(x##2) C10(x##3) C10(x##4) C10(x##5) C10(x##6) C10(x##7) C10(x##8) C10(x##9)
#define C1000(x) C100(x##0) C100(x##1) C100(x##2) C100(x##3) C100(x##4) C100(x##5) C100(x##6) C100(x##7) C100(x##8) C100(x##9)
#define C10000(x) C1000(x##0) C1000(x##1) C1000(x##2) C1000(x##3) C1000(x##4) C1000(x##5) C1000(x##6) C1000(x##7) C1000(x##8) C1000(x##9)
static int select_case(int n)
{
    int result = 0;
    switch (n) {
    C10000(1)
        result = 7;
        break;
    default: result = 9;
    }
    return result;
}
static int mixed(int n)
{
    int a = 0;
    switch (n) {
    case 1: first: case 2: a += 3; break;
    default: case 3: a += 8;
    }
    if (a == 0) goto first;
    return a;
}
int main(void)
{
    if (deep || callback) return 1;
    if (select_case(10000) != 7 || select_case(19999) != 7 || select_case(20000) != 9) return 2;
    if (mixed(1) != 3 || mixed(2) != 3 || mixed(3) != 8 || mixed(4) != 8) return 3;
    if (({ one: two: 17; }) != 17) return 4;
    return 0;
}
