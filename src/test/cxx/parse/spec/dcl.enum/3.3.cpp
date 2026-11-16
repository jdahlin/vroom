// [dcl.enum] Example 3, part 3 (paragraph 10), docs/cxx-spec/dcl/dcl.enum.md
// needs: P4
// status: annotated

enum class Col { red, yellow, green };
int x = Col::red;               // error: no Col to int conversion // semantic
Col y = Col::red;
if (y) { }                      // error: no Col to bool conversion // syntax-error
