/* A C++ exception (thrown and caught by catch_through_c.cpp, built by clang++) passes through C
   frames compiled with -fexceptions: their cleanup variables run, innermost first, and the frames
   between without cleanups pass it on.  A cleanup that runs on the normal path and calls the
   thrower is covered only by the cleanups below it (no pad runs it again). */
void cxx_throw(int k);

int c_order[16], c_n;

static void note(int *p)
{
    c_order[c_n++] = *p;
}

static void note_and_throw(int *p)
{
    c_order[c_n++] = *p;
    cxx_throw(*p);
}

__attribute__((noinline)) static void leaf(int k)
{
    if (k)
        cxx_throw(k);
}

__attribute__((noinline)) static void middle(int k)
{
    int a __attribute__((cleanup(note))) = 10;
    for (int i = 0; i < 2; i++) {
        int b __attribute__((cleanup(note))) = 20 + i;
        leaf(k && i == 1);
    }
}

__attribute__((noinline)) int c_layer(int k)
{
    int c __attribute__((cleanup(note))) = 30;
    middle(k);
    return 7;
}

/* the cleanup of d throws on the normal path: c's cleanup runs, d's does not run again */
__attribute__((noinline)) int c_cleanup_throws(void)
{
    int c __attribute__((cleanup(note))) = 40;
    {
        int d __attribute__((cleanup(note_and_throw))) = 41;
        leaf(0);
    }
    return 0;
}
