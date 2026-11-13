struct empty { struct {} values[4] __attribute__((aligned(16))); };
struct float_empty { union {} first; float value; struct {} last; };
struct ld_empty { long double value; struct {} last; };
union double_empty { union {} first; double value; };
struct float_empty fempty(struct float_empty);
struct ld_empty ldempty(struct ld_empty);
union double_empty dempty(union double_empty);
int empty_va(int, ...);
struct __attribute__((packed)) packed_hfa {
    struct {} before;
    float a, b, c, d;
    union {} after;
};
struct packed_hfa packed_empty(struct packed_hfa);
struct packed_hfa packed_empty_stack(float, float, float, float, float, float,
                                     float, float, struct packed_hfa);
int empty_after_stack(long long, long long, long long, long long,
                      long long, long long, long long, long long, ...);
_Static_assert(sizeof(struct empty) == 0, "empty argument has no bytes");
_Static_assert(_Alignof(struct empty) == 16, "empty argument is over-aligned");
