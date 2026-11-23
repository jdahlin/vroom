/* [temp.inst]/3: a class template's member function with a default argument
   (libc++'s polymorphic_allocator::allocate_bytes); the default argument of
   a specialization's member is instantiated when a call uses it */
struct resource {
    void *allocate(unsigned long n, unsigned long a)
    {
        return reinterpret_cast<void *>(n + a);
    }
};
template <class T> struct allocator_like {
    resource *r;
    void *bytes(unsigned long n, unsigned long a = alignof(long double))
    {
        return r->allocate(n, a);
    }
    int sized(int extra = sizeof(T)) { return extra; }
};
int main()
{
    resource r;
    allocator_like<short> p = {&r};
    unsigned long v = reinterpret_cast<unsigned long>(p.bytes(10));
    return (v == 10 + alignof(long double) ? 0 : 1) + (p.sized() - 2) +
           (p.sized(5) - 5);
}
