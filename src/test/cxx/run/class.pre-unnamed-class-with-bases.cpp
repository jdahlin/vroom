/* [class.pre]/1: a class without a class-head-name may have a base-clause
   (doctest's DiscardOStream, an unnamed std::streambuf as a member) */
extern "C" int printf(const char *, ...);

struct base {
    int n;
    base() : n(3)
    {
    }
    virtual int value()
    {
        return n;
    }
    int twice()
    {
        return value() * 2;
    }
};

struct other {
    int m = 5;
};

static class holder : public base
{
    class : public base
    {
        int value() override
        {
            return 40 + n;
        }
    } inner;

public:
    int run()
    {
        return inner.twice() + twice();
    }
} global;

int main()
{
    other o;
    printf("%d %d\n", global.run(), o.m);
}
