/* [stmt.dcl]/4, [basic.start.dynamic]: arrays of scalars of static storage
   duration with elements that are not constant */
static int next = 3;
static int global[4] = {next, next + 1, 7};

template <class T> T take(int n)
{
    static const T local[4] = {static_cast<T>(next + n), static_cast<T>(2),
                               static_cast<T>(next)};
    return local[0] + local[1] + local[2] + local[3];
}

int main()
{
    int first = take<int>(1);
    next = 100;
    if (first != 9 || take<int>(50) != 9)
        return 1;
    static short two[2][2] = {{static_cast<short>(next), 1}, {2}};
    return global[0] + global[1] + global[2] + global[3] != 14 ||
           two[0][0] + two[0][1] + two[1][0] + two[1][1] != 103;
}
