// [stmt.ambig] Example 1 (paragraph 2), docs/cxx-spec/stmt/stmt.ambig.md
// needs: P4 (T is not declared: the ambiguous statements stay unparsed)
// status: well-formed (as far as the example says)
void example() {
T(a)->m = 7;        // expression-statement
T(a)++;             // expression-statement
T(a,5)<<c;          // expression-statement

T(*d)(int);         //  declaration
T(e)[5];            //  declaration
T(f) = { 1, 2 };    //  declaration
T(*g)(double(3));   //  declaration
}
