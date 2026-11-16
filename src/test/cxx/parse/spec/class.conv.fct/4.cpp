// [class.conv.fct] Example 4 (paragraph 3), docs/cxx-spec/class/class.conv.fct.md
// needs: P5
// status: annotated

operator int [[noreturn]] ();   // error: noreturn attribute applied to a type // syntax-error
