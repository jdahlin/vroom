// [dcl.pre]/4: an attribute-declaration has no effect; [dcl.attr.grammar]/6:
// an attribute-token not specified in this document is implementation-defined
// (clang ignores one it does not know, with a warning).
[[]];
[[unknown_thing]];
[[using clang: unknown]] int x;
[[vendor::unknown(1, 2)]] int y;
[[maybe_unused]] int z;
int sum() { return x + y + z; }
