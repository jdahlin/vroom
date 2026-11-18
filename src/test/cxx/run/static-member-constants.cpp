/* [class.static.data]/4: const static integral members initialized in
   the class, usable in constant expressions; one odr-used is defined at
   namespace scope without an initializer.  Static members of a nested
   class. */
struct Limits {
    static const int size = 4;
    static const unsigned long mask = (1UL << size) - 1;
    static const int used = 9;
    struct Inner {
        static int count;
        static const int base = size * 2;
    };
    int data[size];
};

const int Limits::used;
int Limits::Inner::count = 5;

int read(const int &r) { return r; }

int main()
{
    int bad = 0;
    if (sizeof(Limits) != 4 * sizeof(int)) bad |= 1;
    if (Limits::mask != 15) bad |= 2;
    if (read(Limits::used) != 9) bad |= 4;
    if (Limits::Inner::count + Limits::Inner::base != 13) bad |= 8;
    return bad;
}
