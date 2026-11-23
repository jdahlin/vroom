/*
 * The parser's diagnostics (doc/PLAN-cxx-parse.md): our own, not clang's.
 * [intro.compliance]/2.2 asks for "at least one diagnostic message" for an
 * ill-formed program and leaves its text to the implementation; each
 * message names the rule or the production that was not met.
 *
 * DIAGNOSTIC(name, class, default mapping, -W group or "", format)
 */
DIAGNOSTIC(parse_expected_closing, ERROR, ERROR, "", "expected '%0' to close the '%1' on line %2")
DIAGNOSTIC(parse_unmatched_closing, ERROR, ERROR, "", "'%0' closes nothing: there is no '%1' before it")
DIAGNOSTIC(parse_expected_end_of_declaration, ERROR, ERROR, "",
           "the declaration does not end: expected ';' ([dcl.pre])")
DIAGNOSTIC(parse_expected, ERROR, ERROR, "", "expected %0 ([%1])")
DIAGNOSTIC(parse_violation, ERROR, ERROR, "", "%0 ([%1])")
