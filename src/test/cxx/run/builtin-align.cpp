// __builtin_align_up, __builtin_align_down, __builtin_is_aligned of integers, lowered
int up(int x) { return __builtin_align_up(x, 16); }
long down(long x) { return __builtin_align_down(x, 8); }
unsigned char small(unsigned char x) { return __builtin_align_up(x, 16); }
short neg(short x) { return __builtin_align_down(x, 4); }
bool aligned(unsigned long x) { return __builtin_is_aligned(x, 64); }
int main()
{
    int r = 0;
    if (up(1) != 16 || up(16) != 16 || up(-1) != 0 || up(-17) != -16)
        r |= 1;
    if (down(7) != 0 || down(8) != 8 || down(-1) != -8)
        r |= 2;
    if (small(250) != 0 || small(17) != 32)
        r |= 4;
    if (neg(-5) != -8 || neg(7) != 4)
        r |= 8;
    if (!aligned(128) || aligned(129) || !aligned(0))
        r |= 16;
    return r;
}
