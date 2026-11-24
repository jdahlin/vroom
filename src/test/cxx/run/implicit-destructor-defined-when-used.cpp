// [class.dtor]/11, [class.default.ctor]/4: a special member defaulted on
// its first declaration is defined only when odr-used, so the member
// functions of class templates its body would use are instantiated only
// then ([temp.inst]/4).  A class holding a pointer owner of a type that is
// never completed in this unit compiles (libc++'s default_delete has
// static_assert(sizeof(T) >= 0) in its operator(): SkCanvas's
// STArray<1, std::unique_ptr<sktext::GlyphRunBuilder>>); one whose type is
// completed and whose implicit destructor is used destroys its members.
static int destroyed;

template <class T> struct deleter {
    void operator()(T *p) const
    {
        static_assert(sizeof(T) > 0, "incomplete");
        delete p;
    }
};

template <class T> struct owner {
    T *p = nullptr;
    ~owner()
    {
        if (p)
            deleter<T>()(p);
    }
};

template <class T> struct holder : owner<T> {
};

class Never;
struct Unused {
    holder<Never> h;
    int n = 0;
};

int size_of_unused()
{
    return static_cast<int>(sizeof(Unused));
}

class Later;
struct Used {
    holder<Later> h;
    Used();
    Used(const Used &) = delete;
};

class Later {
public:
    ~Later()
    {
        destroyed++;
    }
};

Used::Used()
{
    h.p = new Later;
}

int main()
{
    {
        Used u;
    }
    if (destroyed != 1)
        return 1;
    return size_of_unused() == static_cast<int>(sizeof(void *)) * 2 ? 0 : 2;
}
