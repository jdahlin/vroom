// C++14 digit separators (Skia's SkVx.h: 0x8000'0000), next to character literals
static_assert(0x8000'0000 == 0x80000000u);
static_assert(0b1'1111'1111 == 511);
static_assert(1'000'000 == 1000000);
static_assert('x' == 120);
#define CAT(a, b) a##b
static_assert(CAT(1'0, 0) == 100);
