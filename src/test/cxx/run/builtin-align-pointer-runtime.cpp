// __builtin_align_up, _down and is_aligned of a pointer and by an alignment
// known only at run time, lowered (the operand evaluated before the alignment)
char buffer[256];
char *up(char *p) { return __builtin_align_up(p, 16); }
char *down(char *p, unsigned long a) { return __builtin_align_down(p, a); }
bool aligned(const char *p, int a) { return __builtin_is_aligned(p, a); }
unsigned runtime_up(unsigned x, unsigned a) { return __builtin_align_up(x, a); }
int order[2];
int next;
unsigned first(unsigned x) { order[next++] = 1; return x; }
unsigned second(unsigned a) { order[next++] = 2; return a; }
int main()
{
    int r = 0;
    char *base = up(buffer);
    if ((reinterpret_cast<unsigned long>(base) & 15) || base < buffer ||
        base >= buffer + 16)
        r |= 1;
    if (down(base + 7, 8) != base || down(base + 8, 8) != base + 8)
        r |= 2;
    if (!aligned(base, 16) || aligned(base + 1, 2) || !aligned(base + 4, 4))
        r |= 4;
    if (runtime_up(17, 8) != 24 || runtime_up(16, 16) != 16 ||
        runtime_up(1, 1) != 1)
        r |= 8;
    if (__builtin_align_up(first(5), second(4)) != 8 || order[0] != 1 ||
        order[1] != 2)
        r |= 16;
    return r;
}
