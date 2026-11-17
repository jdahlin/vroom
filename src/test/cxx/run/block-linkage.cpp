// [basic.link]/7: functions and extern variables declared in blocks are
// members of the innermost enclosing namespace
int g1 = 4;
int later_defined();
namespace N {
int user()
{
    extern int nv;       // N::nv, declared here first
    int twice(int);      // N::twice
    return twice(nv);
}
int nv = 21;
int twice(int x) { return 2 * x; }
}
static int counter;
int main()
{
    extern int g1;
    int helper(int);
    extern int defined_after;
    if (g1 != 4 || helper(1) != 2 || defined_after != 9) return 1;
    {
        int helper(int);
        extern int g1;
        if (helper(g1) != 5) return 2;
    }
    if (N::user() != 42) return 3;
    int later_defined();
    extern int counter;
    counter = later_defined();
    if (counter != 3) return 4;
    return 0;
}
int helper(int x) { return x + 1; }
int defined_after = 9;
int later_defined() { return 3; }
