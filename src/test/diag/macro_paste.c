/* D5.2: a name made by ## in a macro: the caret at the invocation, the note at the paste */
/* diag-differs: clang adds "expanded from here" at <scratch space> (vcc has no scratch buffer) */
#define CAT(a, b) a##b
int i(int k) { return CAT(un, defined_cat) + k; }
