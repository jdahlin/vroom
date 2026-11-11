/* Every active frame is materialized, so traversal does not depend on callers
   compiled with an optional frame-pointer policy. */
#if defined(__x86_64__) || defined(__aarch64__)
static void *volatile frames[7], *volatile returns[7];
static volatile int barrier;
__attribute__((noinline,optnone)) static int walk(int n)
{
    frames[n] = __builtin_frame_address(0);
    returns[n] = __builtin_return_address(0);
    if (n) {
        int r = walk(n - 1);
        barrier += n;
        return r;
    }
    if (__builtin_frame_address(1) != frames[1]) return 1;
    if (__builtin_frame_address(2) != frames[2]) return 2;
    if (__builtin_frame_address(5) != frames[5]) return 3;
    if (__builtin_frame_address(6) != frames[6]) return 4;
    if (__builtin_return_address(1) != returns[1]) return 5;
    if (__builtin_return_address(5) != returns[5]) return 6;
    return 0;
}
#endif
int main(void)
{
#if defined(__x86_64__) || defined(__aarch64__)
    return walk(6);
#else
    return 0;
#endif
}
