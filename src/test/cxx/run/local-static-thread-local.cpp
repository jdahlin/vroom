/* [basic.stc.thread], [ext:clang_c11_thread_local], [ext:gnu_thread_local]: a
   static local that is thread_local, _Thread_local or __thread is one object per
   thread, as one of namespace scope is: in the thread's TLS block, not in .bss.
   Seen from one thread: its address is near a namespace-scope one's (which is
   in the block), not megabytes away in .bss.  vcc's own driver/searchpath.cpp
   keeps its per-thread buffers that way: a shared one made a vcc built by vcc
   crash with two or more compile threads. */
static _Thread_local long anchor;

static long near_anchor(const void *p)
{
    long d = (const char *)p - (const char *)&anchor;
    return d > -(1 << 16) && d < (1 << 16);
}

static int probe()
{
    static _Thread_local long a;
    static __thread long b[4];
    static thread_local long c;
    return near_anchor(&a) + near_anchor(&b[2]) + near_anchor(&c);
}

int main()
{
    return probe() != 3;
}
