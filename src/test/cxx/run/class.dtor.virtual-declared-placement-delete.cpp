// [class.dtor]/12: usual deallocation function lookup for a virtual destructor
// happens at the point of definition of the destructor, not at a declaration
// (e.g. Squirrel's SQRefCounted with a declared virtual ~SQRefCounted() and a
// placement-only operator delete(void *, void *, size_t)).

struct RefCounted {
    int refs = 1;
    RefCounted() = default;
    virtual ~RefCounted();
    void operator delete(void *, void *, decltype(sizeof(0))) {}
    virtual int value() const { return refs; }
};

struct Plain {
    int x = 42;
};

int main()
{
    Plain p;
    if (p.x != 42)
        return 1;
    return 0;
}
