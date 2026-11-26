// [class.base.init]/7, [dcl.init]/17: a member of array type initialized by
// () is value-initialized, also in a constant expression
extern "C" int printf(const char *, ...);

struct Table {
    unsigned char bytes[4][3];
    short words[5];
    constexpr Table() : bytes(), words()
    {
        for (int i = 0; i < 4; i++)
            bytes[i][i % 3] = static_cast<unsigned char>(i + 1);
        words[4] = 7;
    }
};

constexpr Table built;
static_assert(built.bytes[2][2] == 3 && built.bytes[2][0] == 0);
static_assert(built.words[0] == 0 && built.words[4] == 7);

struct Holder {
    int values[8];
    int *pointers[2];
    Holder() : values(), pointers() {}
};

static int used(void)
{
    volatile unsigned char fill[128];
    for (int i = 0; i < 128; i++)
        fill[i] = 0x5a;
    return fill[127];
}

static int zeros(void)
{
    Holder h;
    int sum = 0;
    for (int v : h.values)
        sum += v;
    return sum + (h.pointers[0] != nullptr) + (h.pointers[1] != nullptr);
}

int main()
{
    Table t;
    used();
    printf("%d %d %d %d %d\n", t.bytes[3][0], t.bytes[1][1], t.words[3],
           t.words[4], zeros());
    return 0;
}
