/* [ext:clang_builtin_functions] the coroutine handle builtins libc++'s
   <__coroutine/coroutine_handle.h> calls: __builtin_coro_resume,
   __builtin_coro_destroy, __builtin_coro_done, __builtin_coro_promise and
   __builtin_coro_noop (void *); none runs here: the calls are in a
   function main does not reach. */
typedef unsigned long size_t;

static void drive(void *h, bool go)
{
    if (!go)
        return;
    if (!__builtin_coro_done(h))
        __builtin_coro_resume(h);
    __builtin_coro_destroy(h);
}

static void *promise(void *h)
{
    return __builtin_coro_promise(h, 8, false);
}

static void *noop()
{
    return __builtin_coro_noop();
}

int main(int argc, char **)
{
    if (argc > 9) {
        drive(promise(noop()), true);
    }
    return 0;
}
