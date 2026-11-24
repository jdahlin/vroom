/* [class.default.ctor]/2: a union with a variant member of non-trivial
   default constructor has only a defaulted default constructor deleted; a
   user-provided one is usable (SkTHash's Slot::Storage). */
struct member {
    int a;
    member() : a(1) {}
};

struct slot {
    slot() = default;
    unsigned hash = 0;
    union storage {
        member value;
        storage() {}
        ~storage() {}
    } v;
};

int main()
{
    slot *s = new slot[3];
    unsigned h = s[2].hash;
    delete[] s;
    return static_cast<int>(h);
}
