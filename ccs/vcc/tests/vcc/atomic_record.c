/* vcc-flags: -O1 */
/* Atomic values use one snapshot; rounded atomic padding must not change the
 * ordinary record's representation or make assignments return an address of
 * the atomic destination. */
struct B1 { unsigned char a; };
struct B2 { unsigned short a; };
struct B3 { unsigned char a[3]; };
struct B4 { unsigned a; };
struct B8 { unsigned a, b; };
static _Atomic(struct B8) global;
static int side;
static struct B8 make(void) { side++; return (struct B8){0x12345678, 0x76543210}; }
static __attribute__((noinline)) int scalar(int n)
{
    _Atomic int x = 1;
    for (int i=0; i<n; i++) x += 2;
    _Atomic int y[2] = {3,4};
    y[0] = 8;
    return x + y[0] + y[1];
}
int main(void)
{
    struct B1 p1 = {201}; _Atomic(struct B1) a1 = p1; struct B1 r1 = a1;
    struct B2 p2 = {50001}; _Atomic(struct B2) a2 = p2; struct B2 r2 = a2;
    struct B3 p3 = {{17,131,255}}; _Atomic(struct B3) a3 = p3; struct B3 r3 = a3;
    struct B4 p4 = {0xdeadbeef}; _Atomic(struct B4) a4 = p4; struct B4 r4 = a4;
    struct B8 p8 = {0xabcdef12,0x12876543}; _Atomic(struct B8) a8 = p8; struct B8 r8 = a8;
    if (r1.a != p1.a || r2.a != p2.a || r4.a != p4.a || r8.a != p8.a || r8.b != p8.b) return 1;
    for (int i=0; i<3; i++) if (r3.a[i] != p3.a[i]) return 2;
    struct B8 assigned = (global = make());
    struct B8 snapshot = global;
    global = (struct B8){1,2};
    if (side != 1 || assigned.a != 0x12345678 || assigned.b != 0x76543210 || snapshot.a != assigned.a || snapshot.b != assigned.b) return 3;
    a3 = (struct B3){{4,5,6}};
    r3 = a3;
    if (r3.a[0] != 4 || r3.a[1] != 5 || r3.a[2] != 6) return 4;
    if (scalar(5) != 23) return 5;
    return 0;
}
