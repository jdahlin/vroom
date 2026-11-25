/* [ext:gnu_extension_keyword] __extension__ begins a unary-expression,
   never a type-id: ( __extension__ ... ) is a parenthesized expression,
   not a cast.  glibc's CPU_ISSET_S expands to
   (__extension__ ({ size_t __cpu = (cpu); ... })), which Stockfish's
   numa.h uses. */
typedef unsigned long mask_t;
struct cpu_set {
    mask_t bits[2];
};
#define ISSET(cpu, setsize, set)                                               \
    (__extension__({                                                           \
        unsigned long cpu_ = (cpu);                                            \
        cpu_ / 8 < (setsize)                                                   \
            ? ((((const mask_t *)((set)->bits))[cpu_ / (8 * sizeof(mask_t))] & \
                ((mask_t)1 << (cpu_ % (8 * sizeof(mask_t))))) != 0)            \
            : 0;                                                               \
    }))
int plain(int v) { return (__extension__ v) + (__extension__ 1); }
int main()
{
    cpu_set s = {{0x5, 0x1}};
    if (!ISSET(0, sizeof s, &s) || ISSET(1, sizeof s, &s) ||
        !ISSET(2, sizeof s, &s) || !ISSET(64, sizeof s, &s))
        return 1;
    if (ISSET(200, sizeof s, &s))
        return 2;
    if (plain(2) != 3)
        return 3;
    return 0;
}
