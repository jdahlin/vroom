// [dcl.enum] Example 3 (paragraph 10), docs/cxx-spec/dcl/dcl.enum.md
// needs: P3
// status: annotated

enum color { red, yellow, green=20, blue };
color col = red;
color* cp = &col;
if (*cp == blue)                // ...
