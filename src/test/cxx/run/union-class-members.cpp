/* [class.union]/2: members of class type whose special member functions
   are all trivial (glibc's siginfo_t: a union of unnamed structs) */
struct Pair {
    int a, b;
};

struct Info {
    int signo;
    union {
        int pad[8];
        struct {
            int pid, uid;
        } kill;
        struct {
            int tid;
            Pair p[2];
        } timer;
    } fields;
};

struct Defaulted {
    int x;
    Defaulted() = default;
    Defaulted(int v) : x(v) {}
};

union U {
    Defaulted d;
    float f;
};

static_assert(sizeof(Info) == 36, "");
static_assert(sizeof(U) == 4, "");

int main()
{
    Info i;
    i.signo = 11;
    i.fields.kill.pid = 7;
    i.fields.kill.uid = 9;
    int bad = 0;
    if (i.fields.pad[0] != 7 || i.fields.timer.tid != 7) bad |= 1;
    i.fields.timer.p[1].b = 42;
    if (i.fields.pad[4] != 42) bad |= 2;
    Info j = i;
    if (j.fields.kill.uid != 9 || j.fields.timer.p[1].b != 42) bad |= 4;
    U u;
    u.d.x = 5;
    U w = u;
    if (w.d.x != 5) bad |= 8;
    return bad;
}
