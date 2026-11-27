/* [abi:mangling-type], [abi:mangling-builtin], [abi:mangle.qualified-type],
   [abi:mangle.function-type], [abi:mangle.array-type],
   [abi:mangle.pointer-to-member-type] */
void builtins(void *, wchar_t, bool, char, signed char, unsigned char, short,
              unsigned short, int, unsigned, long, unsigned long, long long,
              unsigned long long, __int128, unsigned __int128, float, double,
              long double, char32_t, char16_t, char8_t, decltype(nullptr))
{
}
void ellipsis(int, ...) {}
void empty() {}
void qualified(const int *, volatile int *, const volatile int *,
               int *__restrict, const int &, int &&)
{
}
void functions(void (*)(int), int (&)(char), void (*)(...))
{
}
struct S {
    int m;
    int f(int) const;
};
void members(int S::*, int (S::*)(int) const) {}
void arrays(int (*)[4], int (&)[2][3], char (*)[]) {}
enum E { e1 };
enum class F : char { f1 };
void enums(E, F) {}
void float16(_Float16) {}
void bitint(_BitInt(7), unsigned _BitInt(9)) {}
// MANGLE: _Z8builtinsPvwbcahstijlmxynofdeDiDsDuDn
// MANGLE: _Z8ellipsisiz
// MANGLE: _Z5emptyv
// MANGLE: _Z9qualifiedPKiPViPVKiPiRS_Oi
// MANGLE: _Z9functionsPFviERFicEPFvzE
// MANGLE: _Z7membersM1SiMS_KFiiE
// MANGLE: _Z6arraysPA4_iRA2_A3_iPA_c
// MANGLE: _Z5enums1E1F
// MANGLE: _Z7float16DF16_
// MANGLE: _Z6bitintDB7_DU9_
