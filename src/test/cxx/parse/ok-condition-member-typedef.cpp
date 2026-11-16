// if (C x = e) in a class template's member, C its member typedef (Skia's
// SkBitSet::forEachSetIndex: if (Chunk chunk = chunks[i]))
template <class FN> struct B {
    using Chunk = unsigned;
    Chunk *c;
    void f(FN fn) const
    {
        for (int i = 0; i < 2; ++i)
            if (Chunk chunk = c[i])
                fn(chunk);
        while (Chunk k{c[0]})
            break;
    }
};
