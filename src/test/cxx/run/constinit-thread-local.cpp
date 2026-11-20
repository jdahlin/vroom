/* [dcl.constinit], [dcl.stc]/3: constinit on variables of static and thread
   storage duration; thread_local at namespace and block scope (static is
   implied there), the thread storage duration of _Thread_local.  The
   variables have internal linkage: for one with external linkage clang
   also defines the thread wrapper function _ZTW... ([abi:thread-local]),
   which vcc does not yet.  syntax.h: inline constinit thread_local const
   T *p = nullptr */
struct ctx {
    int v;
};
static constinit thread_local const ctx *ambient = nullptr;
static constinit int g = 3;
static constinit thread_local int tl = 5;
constexpr int f(int x)
{
    return x + 1;
}
static constinit int h = f(2);
static thread_local int plain;
int main()
{
    thread_local int counter = 0;
    int r = 0;
    counter++;
    tl += g + h;
    plain = 1;
    if (ambient != nullptr)
        r |= 1;
    if (counter != 1)
        r |= 2;
    if (tl != 5 + 3 + 3)
        r |= 4;
    if (plain != 1)
        r |= 8;
    return r;
}
