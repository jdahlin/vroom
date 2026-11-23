/* [expr.prim.lambda.capture]/7, /10, /12: capture-defaults, explicit
   captures by copy and by reference, mutable, capture order */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int global = 5;

int main()
{
    int a = 1, b = 2, c = 3;
    auto copy_all = [=] { return a + b + c; };
    auto ref_all = [&] { a += 10; return a + b; };
    auto mixed = [=, &c] { c += 1; return a + c; };
    auto mixed2 = [&, b] { a += b; return a; };
    auto mut = [a]() mutable { return ++a; };
    check(copy_all() == 6);
    a = 100;
    check(copy_all() == 6);
    check(ref_all() == 112);
    check(a == 110);
    check(mixed() == 114 && c == 4);
    check(mixed2() == 112 && a == 112);
    check(mut() == 113 && mut() == 114 && a == 112);
    auto captured_ref = [&]() -> const int & { return a; };
    check(&captured_ref() == &a);
    /* static storage duration is not captured */
    static int s = 7;
    auto nocapture = [] { return s + global; };
    int (*fp)() = nocapture;
    check(fp() == 12);
    check(sizeof(nocapture) == 1);
    check(sizeof(copy_all) == 3 * sizeof(int));
    check(sizeof(mixed) == sizeof(int) + sizeof(void *) + 0 || sizeof(mixed) >= 12);
    return 0;
}
