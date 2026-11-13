/* VCC_THREADS=6 exercises independent function prepasses sharing globals. */
/* vcc-threads-same */
int counter, assigned, array[2];
double floating;
struct Pair { int x, y; } pair;
__attribute__((noinline)) static void bump(int *p) { ++*p; }
#define ONE(N) __attribute__((noinline)) int work_##N(void) { \
    int local = 3, local_array[2] = { 2, 4 }; \
    ++counter; assigned = 7; array[1] = 9; floating += 1; \
    pair.x += 2; bump(&pair.y); local = 5; local_array[1] = 8; \
    return local + local_array[1]; }
#define TEN(N) ONE(N##a) ONE(N##b) ONE(N##c) ONE(N##d) ONE(N##e) ONE(N##f) ONE(N##g) ONE(N##h) ONE(N##i) ONE(N##j)
#define HUNDRED(N) TEN(N##a) TEN(N##b) TEN(N##c) TEN(N##d) TEN(N##e) TEN(N##f) TEN(N##g) TEN(N##h) TEN(N##i) TEN(N##j)
HUNDRED(a) HUNDRED(b)
#undef ONE
#define ONE(N) work_##N,
int (*functions[])(void) = { HUNDRED(a) HUNDRED(b) };
int main(void) {
    for (unsigned i = 0; i < sizeof(functions)/sizeof(functions[0]); ++i)
        if (functions[i]() != 13) return 1;
    return counter != 200 || assigned != 7 || array[1] != 9 ||
           floating != 200 || pair.x != 400 || pair.y != 200;
}
