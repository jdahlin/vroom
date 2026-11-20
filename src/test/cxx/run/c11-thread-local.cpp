/* [ext:clang_c11_thread_local] _Thread_local in C++: C11's thread storage
   duration (constant initialization, accessed directly, without
   thread_local's wrapper functions; -O0 symbols as clang's): vcc's own
   fail.h, lex.h and stack-guard.h */
extern _Thread_local int counter;
_Thread_local int counter = 5;
static _Thread_local const char *name = "main";
struct pair {
    int a, b;
};
_Thread_local static pair p = {1, 2};
static int bump()
{
    static _Thread_local int calls;
    return ++calls;
}
int *address() { return &counter; }
int main()
{
    bump();
    *address() += 100;
    p.a = 40;
    return counter + bump() + p.a + p.b + name[0] - 'm' - 149;
}
