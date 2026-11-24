// [conv.double]/1: a double beyond the range of float converts to float (an
// infinity) in a constant expression; clang accepts it.  [conv.fpint]/1 is
// the undefined one: a floating value out of an integer type's range.
constexpr float f = (float)1e300;
constexpr float g = 1e300;
constexpr long long h = (long long)1e18;
