/*
 * Semantic analysis's diagnostics (doc/PLAN-cxx-sema.md): our own, not
 * clang's.  [intro.compliance]/2.2 asks for "at least one diagnostic
 * message" for an ill-formed program; each message says the rule that was
 * not met and names its subclause.
 *
 * DIAGNOSTIC(name, class, default mapping, -W group or "", format)
 */
DIAGNOSTIC(sema_violation, ERROR, ERROR, "", "%0 ([%1])")
/* rules clang does not hold a program to unless asked (-pedantic,
   -pedantic-errors): its extensions, mapped as its own diagnostics are */
DIAGNOSTIC(sema_duplicate_specifier, EXTENSION_WARNING, WARNING, "duplicate-decl-specifier",
           "'%0' appears more than once in the decl-specifier-seq ([dcl.spec]/2)")
DIAGNOSTIC(sema_zero_length_array, EXTENSION, IGNORE, "zero-length-array",
           "an array bound shall be greater than zero ([dcl.array]/1)")
DIAGNOSTIC(sema_no_declarator, EXTENSION_WARNING, WARNING, "missing-declarations",
           "the declaration declares nothing: the init-declarator-list can be omitted only when declaring a class or enumeration ([dcl.pre]/5)")
/* [intro.compliance]/8: Microsoft's extensions (-fms-extensions) */
DIAGNOSTIC(sema_static_after_non_static, EXTENSION_WARNING, WARNING, "microsoft-redeclare-static",
           "a name declared static after a declaration with external linkage: the linkages implied by successive declarations shall agree ([dcl.stc]/6)")
/* [dcl.init.list]/7: an error by default in C++11 and later, as clang's
   -Wc++11-narrowing (none before C++11) */
DIAGNOSTIC(sema_narrowing, EXTENSION_WARNING, ERROR, "c++11-narrowing",
           "a narrowing conversion is required: it is ill-formed in a list-initialization ([dcl.init.list]/3)")
/* [diff.lex]/3: a string literal to a pointer to non-const characters,
   clang's -Wwritable-strings from C++11 on */
DIAGNOSTIC(sema_writable_string, EXTENSION_WARNING, WARNING, "writable-strings",
           "a string literal converts to a pointer to const characters: the conversion to char * was removed in C++11 ([diff.lex]/3)")
/* [dcl.constexpr]/6's "no diagnostic required": clang's
   -Winvalid-constexpr, an error by default (until C++23) */
DIAGNOSTIC(sema_constexpr_never_constant, EXTENSION_WARNING, ERROR, "invalid-constexpr",
           "no invocation of this constexpr function is a constant expression, whatever its arguments ([dcl.constexpr]/6)")
/* [stmt.return]/2: clang's -Wreturn-mismatch, an error by default */
DIAGNOSTIC(sema_return_without_value, EXTENSION_WARNING, ERROR, "return-mismatch",
           "a return statement with no operand shall be used only in a function whose return type is cv void ([stmt.return]/2)")
DIAGNOSTIC(sema_return_with_value, EXTENSION_WARNING, ERROR, "return-mismatch",
           "a return statement with an operand that is not of type void shall be used only in a function whose return type is not cv void ([stmt.return]/2)")
