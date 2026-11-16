// [except.throw] Example 1, part 2 (paragraph 1), docs/cxx-spec/except/except.throw.md
// needs: P6
// status: well-formed (as far as the example says)

try {
    // ...
} catch(const char* p) {
    // handle character string exceptions here
}
