/* cxx-run-flags: -frtti */
// [expr.dynamic.cast]: pointer dynamic_cast (upcast, null check, and runtime
// __dynamic_cast call with Itanium ABI type_info objects).

static int calls = 0;

extern "C" {
extern const void *const _ZTVN10__cxxabiv117__class_type_infoE[4] = {};
extern const void *const _ZTVN10__cxxabiv120__si_class_type_infoE[4] = {};
void *__dynamic_cast(const void *sub, const void *src, const void *dst,
                     long offset)
{
    calls++;
    if (!sub || !src || !dst)
        return nullptr;
    (void)offset;
    return const_cast<void *>(sub);
}
}

struct Base {
    int a = 10;
    virtual ~Base() {}
};

struct Derived : Base {
    int b = 20;
};

int main()
{
    Derived d;
    Base *bp = dynamic_cast<Base *>(&d);
    if (!bp || bp->a != 10 || calls != 0)
        return 1;
    Base *null_bp = nullptr;
    Derived *null_dp = dynamic_cast<Derived *>(null_bp);
    if (null_dp != nullptr || calls != 0)
        return 2;
    Derived *dp = dynamic_cast<Derived *>(bp);
    if (!dp || dp->b != 20 || calls != 1)
        return 3;
    return 0;
}
