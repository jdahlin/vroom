/* A call that may unwind (an invoke, here for a destructor's cleanup)
   writes memory: a load after it is not the value stored before it
   (Highway's called_ flag, read by the temporary's destructor) */
extern "C" int printf(const char *, ...);
static int bad;
namespace
{
struct Checked {
    mutable bool called = false;
    ~Checked()
    {
        if (!called)
            bad++;
    }
    __attribute__((noinline)) void operator()(int n) const
    {
        called = true;
        printf("%d\n", n);
    }
};
} // namespace
__attribute__((noinline, flatten)) static void all()
{
    Checked()(1);
    const Checked c;
    c(2);
    Checked()(3);
}
int main()
{
    all();
    return bad;
}
