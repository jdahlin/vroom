// for (init; T x = e; step): a declaration as the condition (Skia's path iterators)
struct I { int *next(); };
void f(I r)
{
    for (auto it = r; auto p = it.next();)
        (void)p;
    for (int i = 0; bool more = i < 3; ++i)
        (void)more;
}
