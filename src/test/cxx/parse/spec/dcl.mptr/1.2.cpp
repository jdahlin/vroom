// [dcl.mptr] Example 1, part 2 (paragraph 2), docs/cxx-spec/dcl/dcl.mptr.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

X obj;
// ...
obj.*pmi = 7;       // assign 7 to an integer member of obj
(obj.*pmf)(7);      // call a function member of obj with the argument 7
