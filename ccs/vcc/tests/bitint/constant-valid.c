/* C23 wide constants retain provenance through scalar and subobject folds. */
constexpr unsigned _BitInt(310) big = 0x100000000000000000000000000000000000000000000000001uwb;
constexpr unsigned _BitInt(310) elements[] = {big, 123};
static_assert(elements[0] == big);
static_assert(elements[1] + 1 == 124);
constexpr struct { _BitInt(256) x; } record = {-100};
static_assert(record.x == -100);
constexpr _BitInt(256) exact_float = (_BitInt(256))0x1.8p200;
static_assert(exact_float == ((_BitInt(256))3 << 199));
int bounded_case(_BitInt(310) x) {
 switch (x) { case (_BitInt(310))-100: return 1; case (_BitInt(310))123: return 2; }
 return 0;
}
