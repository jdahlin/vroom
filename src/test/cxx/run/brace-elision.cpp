/* [dcl.init.aggr]/16: brace elision, a subaggregate initialized from as
   many clauses as it has elements */
struct P { int x, y; };
struct Q { P p; int z; char s[3]; };
Q global = {1, 2, 3, 'a', 'b'};
int grid[2][3] = {1, 2, 3, 4};
int rows[][2] = {1, 2, 3, 4, 5};
struct B { int b; };
struct D : B { int d; };
int main()
{
    Q local = {4, 5, 6, "xy"};
    P pairs[2] = {7, 8, 9};
    D derived = {10, 11};
    if (global.p.x != 1 || global.p.y != 2 || global.z != 3 ||
        global.s[0] != 'a' || global.s[1] != 'b' || global.s[2] != 0)
        return 1;
    if (grid[0][2] != 3 || grid[1][0] != 4 || grid[1][1] != 0)
        return 2;
    if (sizeof(rows) != 3 * sizeof(rows[0]) || rows[2][0] != 5 || rows[2][1])
        return 3;
    if (local.p.y != 5 || local.z != 6 || local.s[1] != 'y')
        return 4;
    if (pairs[1].x != 9 || pairs[1].y != 0)
        return 5;
    if (derived.b != 10 || derived.d != 11)
        return 6;
    return 0;
}
