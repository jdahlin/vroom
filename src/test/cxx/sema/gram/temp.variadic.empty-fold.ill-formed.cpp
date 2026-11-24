// [temp.variadic]/10: an empty unary fold over an operator with no value in
// Table 15 is ill-formed; "," gives void().
template <int... N> void bad() { (N + ...); }
template void bad<>();
