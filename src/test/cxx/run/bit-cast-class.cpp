/* [bit.cast]: trivially copyable classes and __bf16, both ways (Highway's
   float16_t and bfloat16_t) */
struct Half {
    unsigned short bits;
};
struct Pair {
    unsigned lo, hi;
};
struct Floats {
    float a, b;
};

template <class To, class From> To cast(const From &v)
{
    return __builtin_bit_cast(To, v);
}

static Half make() { return Half{7}; }

int main()
{
    Half h = cast<Half>(static_cast<unsigned short>(0x3c00));
    if (h.bits != 0x3c00 || cast<unsigned short>(h) != 0x3c00)
        return 1;
    __bf16 b = cast<__bf16>(h);
    if (cast<unsigned short>(b) != 0x3c00 || cast<Half>(b).bits != 0x3c00)
        return 2;
    Pair p = {0x3f800000u, 0x40000000u};
    Floats f = cast<Floats>(p);
    if (f.a != 1.0f || f.b != 2.0f)
        return 3;
    double d = cast<double>(p);
    if (cast<Pair>(d).hi != p.hi || cast<Pair>(f).lo != p.lo)
        return 4;
    if (__builtin_bit_cast(short, make()) != 7)
        return 5;
    return 0;
}
