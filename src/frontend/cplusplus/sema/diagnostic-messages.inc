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
