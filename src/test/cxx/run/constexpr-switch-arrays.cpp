/* The constant evaluator on switch statements ([stmt.switch]) and arrays
   ([dcl.init.aggr], [expr.sub]): values used in static_assert, array
   bounds and enumerators, and as static data. */
#include <stdio.h>

constexpr int table[] = {3, 1, 4, 1, 5, 9, 2, 6};
constexpr int grid[2][3] = {{1, 2, 3}, {4}};

constexpr int pick(int i) { return table[i]; }

/* fallthrough, continue to the loop, break out of the switch */
constexpr int tally(int n)
{
    int s = 0;
    for (int i = 0; i < n; i++) {
        switch (i % 3) {
        case 0:
            s += 1;
        case 1:
            s += 10;
            continue;
        default:
            s += 100;
            break;
        }
        s += 1000;
    }
    return s;
}

constexpr int classify(int c)
{
    switch (int d = c - '0') {
    default:
        return -1;
    case 0:
    case 1:
        return d;
    case 2: {
        int twice = d * 2;
        return twice;
    }
    }
}

/* a local array: initialized in part, written, sorted */
constexpr int sorted_median()
{
    int a[8] = {};
    for (int i = 0; i < 8; i++)
        a[i] = table[i];
    for (int i = 1; i < 8; i++)
        for (int j = i; j > 0 && a[j - 1] > a[j]; j--) {
            int t = a[j];
            a[j] = a[j - 1];
            a[j - 1] = t;
        }
    return a[3] + a[4];
}

constexpr int column_sum(int j)
{
    int partial[3] = {grid[0][j]};
    partial[1] += grid[1][j];
    ++partial[2];
    return partial[0] + partial[1] + partial[2];
}

struct row { int cells[2]; };
constexpr row empty_rows[2] = {};
static_assert(empty_rows[1].cells[1] == 0, "");

static_assert(pick(5) == 9, "");
static_assert(tally(6) == 2 * (1 + 10 + 10 + 100 + 1000), "");
static_assert(classify('1') == 1 && classify('2') == 4 && classify('x') == -1,
              "");
static_assert(sorted_median() == 7, "");
static_assert(column_sum(0) == 6 && column_sum(2) == 4, "");

int bound[pick(2)];
enum { E = tally(4) };
int g = sorted_median() + column_sum(1);

int main()
{
    int k = 3;
    printf("%d %d %d %d %d\n", pick(k), tally(k), classify('0' + k % 3),
           sorted_median(), column_sum(k - 2));
    printf("%d %d %d\n", (int)sizeof bound, (int)E, g);
    return 0;
}
