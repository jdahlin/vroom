int main(void)
{
#ifdef __x86_64__
    for (int i = 0; i < 32; i++) {
        long double root, angle, cosine, sine;
        double d = 16.0;
        float f;
        __asm__("fsqrt" : "=t"(root) : "0"(25.0L));
        __asm__("fsqrt" : "+t"(d));
        __asm__("fsqrt" : "=t"(f) : "0"(9.0f));
        __asm__("fpatan" : "=t"(angle) : "0"(1.0L), "u"(0.0L) : "st(1)");
        __asm__("fsincos" : "=t"(cosine), "=u"(sine) : "0"(0.0L));
        if (root != 5.0L || d != 4.0 || f != 3.0f || angle != 0.0L ||
            cosine != 1.0L || sine != 0.0L) return 1;
    }
    unsigned output;
    unsigned long input = 0x1234567887654321UL;
    __asm__("" : "=r"(output) : "0"(input));
    if (output != 0x87654321U) return 2;
#endif
    return 0;
}
