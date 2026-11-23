/* [ext:gnu_labels_as_values] GCC manual 6.3: &&label has type void*, goto *e
   jumps to it; a static table of label addresses; the address compared and
   passed out of the function; a threaded-dispatch interpreter loop. */
extern "C" int printf(const char *, ...);

enum op { PUSH1, ADD, DUP, DEC, JNZ, HALT };

static int run(const unsigned char *code)
{
    static const void *const table[] = {&&do_push1, &&do_add, &&do_dup,
                                        &&do_dec,   &&do_jnz, &&do_halt};
    int stack[16];
    int sp = 0;
    const unsigned char *pc = code;
    int steps = 0;
#define NEXT                                                                   \
    do {                                                                       \
        steps++;                                                               \
        goto *table[*pc++];                                                    \
    } while (0)
    NEXT;
do_push1:
    stack[sp++] = 1;
    NEXT;
do_add:
    sp--;
    stack[sp - 1] += stack[sp];
    NEXT;
do_dup:
    stack[sp] = stack[sp - 1];
    sp++;
    NEXT;
do_dec:
    stack[sp - 1]--;
    NEXT;
do_jnz:
    if (stack[--sp])
        pc = code + *pc;
    else
        pc++;
    NEXT;
do_halt:
    printf("steps %d top %d\n", steps, stack[sp - 1]);
    return stack[sp - 1];
}

/* the address leaves the function and is used there */
__attribute__((noinline)) static void *where(int which, void **other)
{
    if (which) {
        *other = &&second;
        return &&first;
    }
    *other = &&first;
    return &&second;
first:
    printf("first\n");
    return nullptr;
second:
    printf("second\n");
    return nullptr;
}

static int pick(int n)
{
    void *t = n > 0 ? &&pos : &&neg;
    goto *t;
pos:
    return 1;
neg:
    return -1;
}

static int via_local_table(int i)
{
    void *labels[3] = {&&l0, &&l1, &&l2};
    int r = 0;
    goto *labels[i];
l0:
    r += 1;
l1:
    r += 10;
l2:
    r += 100;
    return r;
}

int main()
{
    /* push1 push1 add dup dec jnz ... : count 3 down from 3 */
    const unsigned char prog[] = {PUSH1, PUSH1, ADD, PUSH1, ADD, DEC,
                                  DUP,   JNZ,   5,   HALT};
    int r = run(prog);
    void *o1 = nullptr, *o2 = nullptr;
    void *a = where(1, &o1);
    void *b = where(0, &o2);
    printf("cross %d %d null %d\n", a == o2, b == o1, a == nullptr);
    printf("%d %d\n", pick(5), pick(-5));
    printf("%d %d %d\n", via_local_table(0), via_local_table(1),
           via_local_table(2));
    return r == 0 ? 0 : 1;
}
