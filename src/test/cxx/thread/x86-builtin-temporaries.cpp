/* x86's vector builtins (frontend/c/builtins-x86.cpp: pmovmskb128, the shifts) make scratch
   variables to read a vector's lanes: locals of the function.  Made at scope depth 0 they were
   file-scope statics in .bss, one object for every thread: vcc built by vcc lexed comments
   wrongly with two compile threads (lexer/simd-search.h, _mm_movemask_epi8).  Four threads
   each take the byte-sign mask of their own vector, many times; each must see its own. */
#include <pthread.h>
#include <stdio.h>
#if defined(__x86_64__)
typedef char v16qi __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));

static int mismatches;

static __attribute__((noinline)) int mask_of(v16qi v) { return __builtin_ia32_pmovmskb128(v); }
static __attribute__((noinline)) v4si shift_of(v4si v, int count) { return __builtin_ia32_pslldi128(v, count); }

static void *worker(void *arg)
{
    long id = (long)arg;
    v16qi v;
    for (int i = 0; i < 16; i++)
        v[i] = (id >> (i % 4)) & 1 ? -1 : 0;
    int want = 0;
    for (int i = 0; i < 16; i++)
        want |= (v[i] < 0) << i;
    v4si s = {(int)id + 1, 2, 3, 4};
    for (int i = 0; i < 200000; i++) {
        if (mask_of(v) != want)
            __atomic_fetch_add(&mismatches, 1, __ATOMIC_SEQ_CST);
        v4si r = shift_of(s, 2);
        if (r[0] != ((int)id + 1) << 2 || r[3] != 16)
            __atomic_fetch_add(&mismatches, 1, __ATOMIC_SEQ_CST);
    }
    return 0;
}

int main()
{
    pthread_t t[4];
    for (long i = 0; i < 4; i++)
        pthread_create(&t[i], 0, worker, (void *)(i + 1));
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], 0);
    printf("%d mismatches\n", mismatches);
    return mismatches != 0;
}
#else
int main()
{
    return 0;
}
#endif
