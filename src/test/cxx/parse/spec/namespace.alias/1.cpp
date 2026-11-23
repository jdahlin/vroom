// [namespace.alias] Example 1 (paragraph 3), docs/cxx-spec/dcl/namespace.alias.md
// needs: P5
// status: well-formed (as far as the example says)

namespace Company_with_very_long_name { /* ... */ }
namespace CWVLN = Company_with_very_long_name;
namespace CWVLN = Company_with_very_long_name;  // OK: duplicate
namespace CWVLN = CWVLN;
