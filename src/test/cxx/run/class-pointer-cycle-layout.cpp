/* [abi:class-types]: a class that names, through a pointer, a class that
   holds it by value.  The struct of the class reached through the pointer
   is made after the class that named it, so that its member of the first
   class is the whole struct and not one still being made (whose offsets
   would be zero). */
struct state {
    long a[5];
    struct cache *cache;
};

struct cache {
    struct snapshot *snap;
};

struct snapshot {
    state s;
    struct {
        unsigned long bytes;
        const char **pages;
    } arrays[3];
    char tail;
};

static_assert(__builtin_offsetof(snapshot, arrays) == 48, "");
static_assert(sizeof(snapshot) == 104, "");

int main()
{
    state s;
    snapshot x;
    cache c;
    c.snap = &x;
    s.cache = &c;
    x.s.a[4] = 3;
    x.arrays[2].bytes = 5;
    x.arrays[1].pages = 0;
    x.tail = 7;
    return s.cache->snap->s.a[4] + static_cast<int>(x.arrays[2].bytes) +
           x.tail - 15;
}
