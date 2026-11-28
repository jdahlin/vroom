/* [temp.deduct.call]/4.2: a deduced A reached from A by a qualification
   conversion of two levels (Catch's Session::run(argc, argv)). */
template <typename C> int run(int n, C const *const argv[])
{
    return n + argv[0][0] + static_cast<int>(sizeof(C));
}

int main()
{
    char a[] = "a";
    char *argv[] = {a, nullptr};
    return run(1, argv) != 'a' + 2;
}
