// [except.throw] Example 1, part 4 (paragraph 1), docs/cxx-spec/except/except.throw.md
// needs: P6
// status: well-formed (as far as the example says)

try {
    f(1.2);
} catch(Overflow& oo) {
    // handle exceptions of type Overflow here
}
