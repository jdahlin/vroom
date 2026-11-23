// [expr.throw] Example 1 (paragraph 3), docs/cxx-spec/expr/expr.throw.md
// needs: P6
// status: well-formed (as far as the example says)

try {
  // ...
} catch (...) {     // catch all exceptions
  // respond (partially) to exception
  throw;            // pass the exception to some other handler
}
