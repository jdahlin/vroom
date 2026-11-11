/* Microsoft bit-field allocation, distinct from the target default ABI. */
struct __attribute__((ms_struct)) Units {
    unsigned char a : 3;
    unsigned int b : 7;
    unsigned int c : 5;
    char tail;
};
struct __attribute__((ms_struct)) Zero {
    char a;
    int : 0; /* ignored after a non-bitfield */
    char b;
};
struct __attribute__((ms_struct)) Align {
    char a : 3;
    int : 0;
    char b;
};
union __attribute__((ms_struct)) Whole {
    unsigned long long bits : 3;
    char byte;
};
#pragma pack(push, 1)
struct __attribute__((ms_struct)) Packed {
    char a;
    unsigned int b : 3;
    char c;
};
#pragma pack(pop)
#pragma ms_struct on
struct Enabled { unsigned char a : 3; unsigned int b : 3; };
#pragma ms_struct reset
struct Disabled { unsigned char a : 3; unsigned int b : 3; };
_Static_assert(sizeof(struct Units) == 12, "separate units by base size");
_Static_assert(__builtin_offsetof(struct Units, tail) == 8, "reserve entire unit");
_Static_assert(sizeof(struct Zero) == 2, "ignore first zero-width field");
_Static_assert(sizeof(struct Align) == 8 && _Alignof(struct Align) == 4, "zero-width alignment");
_Static_assert(sizeof(union Whole) == 8 && _Alignof(union Whole) == 1, "union unit without alignment");
_Static_assert(sizeof(struct Packed) == 6, "packed units still reserve base size");
_Static_assert(sizeof(struct Enabled) == 8 && sizeof(struct Disabled) == 4, "pragma state");
int main(void)
{
    struct Units u = { 5, 67, 19, 73 };
    struct Packed p = { 21, 5, 33 };
    if (u.a != 5 || u.b != 67 || u.c != 19 || u.tail != 73) return 1;
    u.b += 1;
    p.b = 3;
    return u.a != 5 || u.b != 68 || u.c != 19 || u.tail != 73 ||
           p.a != 21 || p.b != 3 || p.c != 33;
}
