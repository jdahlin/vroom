/* Embedded zero code units must not split linker-mergeable literals. */
static const unsigned short *p16 = u"a\0b\0\0";
static const unsigned int *p32 = U"a\0b\0\0";
static const __WCHAR_TYPE__ *pw = L"a\0b\0\0";

int main(void)
{
    const unsigned short a16[] = u"a\0b\0\0";
    const unsigned int a32[] = U"a\0b\0\0";
    const __WCHAR_TYPE__ aw[] = L"a\0b\0\0";
    const unsigned int tail[] = U"0123456789012345678901234567890123456789\0\0\0";
    const unsigned expected[] = {97, 0, 98, 0, 0, 0};
    for (unsigned i = 0; i < 6; ++i)
        if (p16[i] != expected[i] || p32[i] != expected[i] || pw[i] != expected[i] ||
            a16[i] != expected[i] || a32[i] != expected[i] || aw[i] != expected[i])
            return 1;
    for (unsigned i = 0; i < sizeof(tail) / sizeof(*tail); ++i)
        if (tail[i] != (i < 40 ? '0' + i % 10 : 0)) return 2;
    return 0;
}
