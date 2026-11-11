/* Memory-forwarding/DSE semantics. Keep independent cells, overlapping byte
 * accesses, escaped addresses and observable accesses distinct. No libc or
 * target assembly is required; the empty asm barrier is native-only. */
#define NI __attribute__((noinline))

struct pair { unsigned a, b; };
static volatile unsigned observed;
static unsigned *saved;
static unsigned *saved_end;

static NI unsigned change(unsigned *p, unsigned x)
{
    unsigned old = *p;
    *p = x;
    return old;
}

static NI void retain(unsigned *p, unsigned *end)
{
    saved = p;
    saved_end = end;
}

static NI unsigned adjacent(unsigned x)
{
    struct pair p;
    p.a = x;
    p.b = 19;
    unsigned first = p.a;
    p.b = 31;
    return first + p.a + p.b;
}

static NI unsigned byte_overlap(unsigned x)
{
    unsigned value = x;
    unsigned char *bytes = (unsigned char *)&value;
    /* Byte inspection defines this without assuming host byte order. */
    unsigned before = bytes[0];
    bytes[0] = 0x73;
    unsigned after = bytes[0];
    bytes[sizeof value - 1] = 0x42;
    return before + after + bytes[sizeof value - 1];
}

static NI unsigned whole_then_fields(unsigned x)
{
    struct pair p = { x, 17 };
    struct pair q = p;
    q.a = 29;
    p.b = 41;
    return p.a + p.b + q.a + q.b;
}

static NI unsigned scopes(unsigned x)
{
    unsigned result;
    { unsigned first[2] = { x, 11 }; result = first[0] + first[1]; }
    { unsigned second[2] = { 37, 43 }; result += second[0] + second[1]; }
    { unsigned char third[2] = { 251, 3 }; result += third[0] + third[1]; }
    return result;
}

static NI unsigned narrow(unsigned x)
{
    unsigned char u = (unsigned char)x;
    signed char s = -7;
    unsigned short h = (unsigned short)(x + 2);
    unsigned a = u;
    int b = s;
    unsigned c = h;
    u = 13;
    s = -3;
    h = 17;
    return a + (unsigned)(b + 100) + c + u + (unsigned)(s + 100) + h;
}

static NI unsigned escaped(unsigned x)
{
    unsigned cells[2] = { x, 23 };
    retain(cells, cells + 2);
    unsigned first = change(saved, 31);
    /* Subtraction and indexing stay within the same array, including the
     * legal one-past pointer. Never compare unrelated stack objects. */
    saved_end[-1] = 41;
    unsigned result = first + cells[0] + cells[1] + (unsigned)(saved_end - saved);
    saved = 0;
    saved_end = 0;
    return result;
}

static NI unsigned escaped_field(unsigned x)
{
    struct pair p = { x, 17 };
    unsigned first = change(&p.a, 31);
    unsigned second = change(&p.b, 41);
    return first + second + p.a + p.b;
}

static NI unsigned source_clobber(unsigned x)
{
    unsigned cells[2];
    cells[0] = x;
    unsigned before = cells[0];
    x = x * 13 + 7;
    cells[1] = x;
    x = change(&cells[1], 19);
    return before + cells[0] + cells[1] + x;
}

static NI unsigned joined(unsigned x, int branch)
{
    struct pair p = { x, 5 };
    unsigned before = p.a;
    if (branch) { p.a = 17; p.b = 23; }
    else { p.a = 31; p.b = 37; }
    return before + p.a + p.b;
}

static NI unsigned looped(unsigned n)
{
    unsigned cells[2] = { 3, 7 };
    for (unsigned i = 0; i != n; ++i) {
        unsigned previous = cells[0];
        cells[0] = cells[1] + i;
        cells[1] = previous + 2;
    }
    return cells[0] * 10 + cells[1];
}

static NI unsigned volatile_cells(unsigned x)
{
    volatile unsigned cells[2];
    cells[0] = x;
    unsigned first = cells[0];
    cells[0] = 17;
    cells[0] = 31;
    /* This store is intentionally unread: runtime output alone cannot prove
     * preservation, so retain the named function for future codegen checks. */
    cells[1] = 43;
    unsigned last = cells[0];
    observed = first + last;
    return observed;
}

static NI unsigned atomic_cells(unsigned x)
{
    unsigned cell;
    __atomic_store_n(&cell, x, __ATOMIC_RELAXED);
    unsigned first = __atomic_load_n(&cell, __ATOMIC_RELAXED);
    unsigned second = __atomic_fetch_add(&cell, 7, __ATOMIC_SEQ_CST);
    __atomic_store_n(&cell, 31, __ATOMIC_RELEASE);
    return first + second + __atomic_load_n(&cell, __ATOMIC_ACQUIRE);
}

static NI unsigned asm_memory(unsigned x)
{
    unsigned cell = x;
#if defined(__x86_64__) || defined(__aarch64__)
    /* A read/write operand models a possible update and must invalidate a
     * forwarded value even though this particular asm preserves the bytes. */
    __asm__ volatile ("" : "+m"(cell) : : "memory");
#endif
    unsigned first = cell;
    change(&cell, 29);
#if defined(__x86_64__) || defined(__aarch64__)
    __asm__ volatile ("" : : "m"(cell) : "memory");
#endif
    return first + cell;
}

int main(void)
{
    if (adjacent(7) != 45) return 1;
    if (byte_overlap(~0u) != 436) return 2;
    if (whole_then_fields(7) != 94) return 3;
    if (scopes(7) != 352) return 4;
    if (narrow(0xfffffff9u) != 66000) return 5;
    if (escaped(7) != 81) return 6;
    if (escaped_field(7) != 96) return 7;
    if (source_clobber(7) != 131) return 8;
    if (joined(7, 0) != 75 || joined(7, 1) != 47) return 9;
    if (looped(0) != 37 || looped(1) != 75 || looped(4) != 123) return 10;
    if (volatile_cells(7) != 38 || observed != 38) return 11;
    if (atomic_cells(7) != 45) return 12;
    if (asm_memory(7) != 36) return 13;
    return 0;
}
