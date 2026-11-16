// [dcl.enum] Example 3, part 2 (paragraph 10), docs/cxx-spec/dcl/dcl.enum.md
// needs: P3
// status: annotated

color c = 1;                    // error: type mismatch, no conversion from int to color // semantic
int i = yellow;                 // OK: yellow converted to integral value 1, integral promotion
