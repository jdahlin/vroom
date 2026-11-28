/* [basic.start.static]/2, [expr.const], [abi:pod]: constant initialization of
   a static object whose base's tail padding the derived class reuses
   (OpenTTD's OldPersistentStorage). */
struct Base {
    unsigned grfid = 0;
    unsigned char feature = 7;
    unsigned tile = 0xffffffffu;
    virtual int kind() const { return 1; }
};

template <class T, int N>
struct Array : Base {
    T storage[N]{};
    T *previous{};
    int kind() const override { return 2; }
};

struct Head {
    long a = 5;
    char b = 'b';
    virtual void touch() {}
};

struct Tail : Head {
    char c = 'c';
    short d = 9;
};

static Array<int, 4> numbers;
Tail tail;

int main()
{
    if (numbers.grfid || numbers.feature != 7 || numbers.tile != 0xffffffffu)
        return 1;
    const Base *b = &numbers;
    if (b->kind() != 2 || numbers.storage[3] || numbers.previous)
        return 2;
    if (tail.a != 5 || tail.b != 'b' || tail.c != 'c' || tail.d != 9)
        return 3;
    numbers.storage[0] = 11;
    numbers.tile = 3;
    if (numbers.storage[0] != 11 || numbers.tile != 3)
        return 4;
    Array<int, 4> local;
    if (local.tile != 0xffffffffu || local.kind() != 2)
        return 5;
    return sizeof(Array<int, 4>) != 48;
}
