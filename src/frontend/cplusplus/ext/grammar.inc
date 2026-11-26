/*
 * The grammar the extensions of extensions.inc add, in the notation of
 * [syntax]: a nonterminal, an alternative of it, terminals as they are
 * spelled, _opt for the subscript.  This is the extensions' counterpart of
 * the standard's bnf blocks (docs/cxx-spec) and of the ABI's mangling
 * grammar (docs/itanium-abi-grammar.tsv): code that parses an extension
 * cites it as [ext:NAME] and quotes its productions, and
 * bench/cxx-spec-refs.py checks each quote against these rows, as it
 * checks the standard's.
 *
 * PRODUCTION(extension, nonterminal, alternative)
 *     A standard nonterminal: an alternative the extension adds to it (not
 *     one the standard has).  Any other nonterminal is the extension's own
 *     and begins with its vendor (gnu-, clang-, microsoft-), so that it is
 *     never taken for the standard's.  Every nonterminal an alternative
 *     uses is the standard's or one defined here.
 * TERMINALS(extension, nonterminal, where)
 *     A nonterminal whose alternatives are keywords listed elsewhere.
 *
 * An extension that adds no syntax (another spelling of a keyword, a
 * pragma's meaning, a predefined macro) has no rows.
 */
#ifndef PRODUCTION
#define PRODUCTION(extension, nonterminal, alternative)
#endif
#ifndef TERMINALS
#define TERMINALS(extension, nonterminal, where)
#endif

/* ---- the preprocessing directives ([cpp.pre]/1's control-line) ---- */

/* GNU cpp manual 9, Preprocessor Output; flags 1 to 4 */
PRODUCTION(gnu_line_marker, "control-line", "# digit-sequence \" s-char-sequence_opt \" pp-tokens_opt new-line")
/* GNU cpp manual 2.7, Wrapper Headers */
PRODUCTION(gnu_include_next, "control-line", "# include_next pp-tokens new-line")
/* GNU cpp manual 11, Other Directives */
PRODUCTION(gnu_ident_directive, "control-line", "# ident pp-tokens new-line")
PRODUCTION(gnu_ident_directive, "control-line", "# sccs pp-tokens new-line")
/* GCC manual 6.21, Variadic Macros: #define f(args...) */
PRODUCTION(gnu_named_variadic_macro, "control-line",
           "# define identifier lparen identifier ... ) replacement-list new-line")
PRODUCTION(gnu_named_variadic_macro, "control-line",
           "# define identifier lparen identifier-list , identifier ... ) replacement-list new-line")
/* Objective-C's #import */
PRODUCTION(clang_import_directive, "control-line", "# import pp-tokens new-line")
/* C23 6.10.4 (C++26's [cpp.embed]) */
PRODUCTION(clang_embed, "control-line", "# embed pp-tokens new-line")
/* (MSVC's __pragma ( pp-tokens_opt ) is an operator like _Pragma, which
   no production has either: [cpp.pragma.op]) */

/* clang, P2173R1 (C++23): attributes after the lambda-introducer or its
   template-parameter-list */
PRODUCTION(clang_lambda_attributes, "lambda-expression",
           "lambda-introducer attribute-specifier-seq lambda-declarator_opt compound-statement")
PRODUCTION(clang_lambda_attributes, "lambda-expression",
           "lambda-introducer < template-parameter-list > requires-clause_opt attribute-specifier-seq lambda-declarator_opt compound-statement")

/* ---- GCC's attributes (GCC manual 6.39, Attribute Syntax) ---- */

PRODUCTION(gnu_attributes, "gnu-attribute-specifier", "__attribute__ ( ( gnu-attribute-list ) )")
PRODUCTION(gnu_attributes, "gnu-attribute-specifier-seq", "gnu-attribute-specifier-seq_opt gnu-attribute-specifier")
PRODUCTION(gnu_attributes, "gnu-attribute-list", "gnu-attribute_opt")
PRODUCTION(gnu_attributes, "gnu-attribute-list", "gnu-attribute-list , gnu-attribute_opt")
/* (a keyword is an identifier there, as in [dcl.attr.grammar]/5) */
PRODUCTION(gnu_attributes, "gnu-attribute", "identifier")
PRODUCTION(gnu_attributes, "gnu-attribute", "identifier ( gnu-attribute-arguments_opt )")
PRODUCTION(gnu_attributes, "gnu-attribute-arguments", "identifier")
PRODUCTION(gnu_attributes, "gnu-attribute-arguments", "identifier , expression-list")
PRODUCTION(gnu_attributes, "gnu-attribute-arguments", "expression-list")
/* where the standard's attribute-specifiers go, and the places only
   GCC's syntax has: among the decl-specifiers, after a declarator (with a
   declarator-id), after a namespace's name */
PRODUCTION(gnu_attributes, "attribute-specifier", "gnu-attribute-specifier")
PRODUCTION(gnu_attributes, "decl-specifier", "gnu-attribute-specifier")
/* MSVC's (clang's ParseMicrosoftDeclSpecs), where GCC's attributes go; the
   parser's node is gnu-attribute-specifier's */
PRODUCTION(microsoft_declspec, "microsoft-declspec", "__declspec ( gnu-attribute-list )")
PRODUCTION(microsoft_declspec, "attribute-specifier", "microsoft-declspec")
PRODUCTION(microsoft_declspec, "decl-specifier", "microsoft-declspec")
PRODUCTION(gnu_attributes, "declarator", "ptr-declarator gnu-attribute-specifier-seq")
PRODUCTION(gnu_attributes, "declarator",
           "noptr-declarator parameters-and-qualifiers trailing-return-type gnu-attribute-specifier-seq")
PRODUCTION(gnu_attributes, "named-namespace-definition",
           "inline_opt namespace attribute-specifier-seq_opt identifier gnu-attribute-specifier-seq { namespace-body }")
PRODUCTION(gnu_attributes, "unnamed-namespace-definition",
           "inline_opt namespace attribute-specifier-seq_opt gnu-attribute-specifier-seq { namespace-body }")

/* clang Attributes, availability: a gnu-attribute whose identifier is
   availability (or __availability__), in any gnu-attribute-list (which
   these rows cannot say: an extension adds no alternative to another's
   nonterminal) */
PRODUCTION(clang_availability, "attribute-specifier", "__attribute__ ( ( clang-availability-attribute ) )")
PRODUCTION(clang_availability, "clang-availability-attribute",
           "identifier ( identifier , clang-availability-clause-list )")
PRODUCTION(clang_availability, "clang-availability-clause-list", "clang-availability-clause")
PRODUCTION(clang_availability, "clang-availability-clause-list",
           "clang-availability-clause-list , clang-availability-clause")
PRODUCTION(clang_availability, "clang-availability-clause", "identifier")
PRODUCTION(clang_availability, "clang-availability-clause", "identifier = pp-number")
PRODUCTION(clang_availability, "clang-availability-clause", "identifier = string-literal")

/* ---- GCC manual 6.47.4, Controlling Names Used in Assembler Code ---- */

PRODUCTION(gnu_asm_labels, "gnu-asm-label", "asm ( string-literal )")
PRODUCTION(gnu_asm_labels, "declarator", "ptr-declarator gnu-asm-label gnu-attribute-specifier-seq_opt")
PRODUCTION(gnu_asm_labels, "declarator",
           "noptr-declarator parameters-and-qualifiers trailing-return-type gnu-asm-label gnu-attribute-specifier-seq_opt")

/* ---- GCC manual 7.3, Restricting Pointer Aliasing ---- */

PRODUCTION(gnu_restrict, "cv-qualifier", "__restrict")
PRODUCTION(gnu_restrict, "cv-qualifier", "__restrict__")

/* ---- clang Language Extensions, Nullability ---- */

PRODUCTION(clang_nullability, "cv-qualifier", "_Nonnull")
PRODUCTION(clang_nullability, "cv-qualifier", "_Nullable")
PRODUCTION(clang_nullability, "cv-qualifier", "_Nullable_result")
PRODUCTION(clang_nullability, "cv-qualifier", "_Null_unspecified")

/* ---- GCC manual 6.48, Alternate Keywords: __extension__ ---- */

PRODUCTION(gnu_extension_keyword, "declaration", "__extension__ declaration")
PRODUCTION(gnu_extension_keyword, "block-declaration", "__extension__ block-declaration")
PRODUCTION(gnu_extension_keyword, "member-declaration", "__extension__ member-declaration")
PRODUCTION(gnu_extension_keyword, "unary-expression", "__extension__ cast-expression")

/* ---- GCC manual 6.9, 128-bit Integers ---- */

PRODUCTION(gnu_int128, "simple-type-specifier", "__int128")

/* ---- GCC manual 6.12, Additional Floating Types ---- */

PRODUCTION(gnu_float128, "simple-type-specifier", "__float128")

PRODUCTION(clang_float16, "simple-type-specifier", "_Float16")
PRODUCTION(clang_float16, "simple-type-specifier", "__bf16")

/* ---- GCC manual 6.7, Referring to a Type with typeof ---- */

PRODUCTION(gnu_typeof, "simple-type-specifier", "gnu-typeof-specifier")
PRODUCTION(gnu_typeof, "gnu-typeof-specifier", "typeof ( expression )")
PRODUCTION(gnu_typeof, "gnu-typeof-specifier", "typeof ( type-id )")

/* ---- GCC manual 6.46, Inquiring on Alignment of Types or Variables ---- */

PRODUCTION(gnu_alignof_expression, "unary-expression", "alignof unary-expression")

/* ---- clang Language Extensions, __datasizeof ---- */

PRODUCTION(clang_datasizeof, "unary-expression", "__datasizeof unary-expression")
PRODUCTION(clang_datasizeof, "unary-expression", "__datasizeof ( type-id )")

/* ---- GCC manual 6.47, Using Assembly Language with C ---- */

/* "::" counts as two ":" (an empty section).  An operand is
   [ symbolic-name ] constraint ( expression ); the clobbers are string
   literals, the labels of asm goto identifiers */
PRODUCTION(gnu_asm_statements, "statement", "gnu-asm-statement")
PRODUCTION(gnu_asm_statements, "gnu-asm-statement", "asm gnu-asm-qualifier-seq_opt ( string-literal ) ;")
PRODUCTION(gnu_asm_statements, "gnu-asm-statement",
           "asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt ) ;")
PRODUCTION(gnu_asm_statements, "gnu-asm-statement",
           "asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt : gnu-asm-operand-list_opt ) ;")
PRODUCTION(gnu_asm_statements, "gnu-asm-statement",
           "asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt : gnu-asm-operand-list_opt : gnu-asm-clobber-list_opt ) ;")
PRODUCTION(gnu_asm_statements, "gnu-asm-statement",
           "asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt : gnu-asm-operand-list_opt : gnu-asm-clobber-list_opt : gnu-asm-label-list_opt ) ;")
PRODUCTION(gnu_asm_statements, "gnu-asm-qualifier-seq", "gnu-asm-qualifier-seq_opt gnu-asm-qualifier")
PRODUCTION(gnu_asm_statements, "gnu-asm-qualifier", "volatile")
PRODUCTION(gnu_asm_statements, "gnu-asm-qualifier", "inline")
PRODUCTION(gnu_asm_statements, "gnu-asm-qualifier", "goto")
PRODUCTION(gnu_asm_statements, "gnu-asm-operand-list", "gnu-asm-operand")
PRODUCTION(gnu_asm_statements, "gnu-asm-operand-list", "gnu-asm-operand-list , gnu-asm-operand")
PRODUCTION(gnu_asm_statements, "gnu-asm-operand", "string-literal ( expression )")
PRODUCTION(gnu_asm_statements, "gnu-asm-operand", "[ identifier ] string-literal ( expression )")
PRODUCTION(gnu_asm_statements, "gnu-asm-clobber-list", "string-literal")
PRODUCTION(gnu_asm_statements, "gnu-asm-clobber-list", "gnu-asm-clobber-list , string-literal")
PRODUCTION(gnu_asm_statements, "gnu-asm-label-list", "identifier")
PRODUCTION(gnu_asm_statements, "gnu-asm-label-list", "gnu-asm-label-list , identifier")

/* ---- GCC manual 6.12, Complex Numbers ---- */

PRODUCTION(gnu_complex, "simple-type-specifier", "_Complex")
PRODUCTION(gnu_complex, "unary-expression", "__real__ cast-expression")
PRODUCTION(gnu_complex, "unary-expression", "__imag__ cast-expression")

/* ---- GCC manual 6.3, Labels as Values ---- */

PRODUCTION(gnu_labels_as_values, "unary-expression", "gnu-label-address")
PRODUCTION(gnu_labels_as_values, "gnu-label-address", "&& identifier")
PRODUCTION(gnu_labels_as_values, "jump-statement", "gnu-computed-goto")
PRODUCTION(gnu_labels_as_values, "gnu-computed-goto", "goto * expression ;")

/* ---- GCC manual 6.8, Conditionals with Omitted Operands ---- */

PRODUCTION(gnu_conditional_omitted, "conditional-expression",
           "logical-or-expression ? : assignment-expression")

/* ---- GCC manual 6.29, Designated Initializers ---- */

PRODUCTION(gnu_designated_initializers, "braced-init-list", "{ gnu-initializer-list ,_opt }")
PRODUCTION(gnu_designated_initializers, "gnu-initializer-list", "gnu-designation_opt initializer-clause")
PRODUCTION(gnu_designated_initializers, "gnu-initializer-list",
           "gnu-initializer-list , gnu-designation_opt initializer-clause")
/* (the second only before a braced-init-list, as C++20's .a{1}) */
PRODUCTION(gnu_designated_initializers, "gnu-designation", "gnu-designator-list =")
PRODUCTION(gnu_designated_initializers, "gnu-designation", "gnu-designator-list")
PRODUCTION(gnu_designated_initializers, "gnu-designation", "identifier :")
PRODUCTION(gnu_designated_initializers, "gnu-designator-list", "designator")
PRODUCTION(gnu_designated_initializers, "gnu-designator-list", "gnu-designator-list designator")
PRODUCTION(gnu_designated_initializers, "designator", "[ constant-expression ]")
PRODUCTION(gnu_designated_initializers, "designator", "[ constant-expression ... constant-expression ]")

/* ---- GCC manual 6.30, Compound Literals ---- */

PRODUCTION(gnu_compound_literals, "postfix-expression", "gnu-compound-literal")
PRODUCTION(gnu_compound_literals, "gnu-compound-literal", "( type-id ) braced-init-list")

/* ---- GCC manual 6.53, Support for offsetof ---- */

PRODUCTION(gnu_offsetof, "primary-expression", "gnu-offsetof-expression")
PRODUCTION(gnu_offsetof, "gnu-offsetof-expression", "__builtin_offsetof ( type-id , gnu-offsetof-member-designator )")
PRODUCTION(gnu_offsetof, "gnu-offsetof-member-designator", "identifier")
PRODUCTION(gnu_offsetof, "gnu-offsetof-member-designator", "gnu-offsetof-member-designator . identifier")
PRODUCTION(gnu_offsetof, "gnu-offsetof-member-designator", "gnu-offsetof-member-designator [ expression ]")

/* ---- GCC manual, Variable Arguments: __builtin_va_arg ---- */

PRODUCTION(gnu_va_arg, "primary-expression", "gnu-va-arg-expression")
PRODUCTION(gnu_va_arg, "gnu-va-arg-expression", "__builtin_va_arg ( assignment-expression , type-id )")

/* ---- GCC manual 6.1, Statements and Declarations in Expressions ---- */

PRODUCTION(gnu_statement_expressions, "primary-expression", "( compound-statement )")

/* ---- clang Language Extensions, __builtin_bit_cast ---- */

PRODUCTION(clang_bit_cast, "primary-expression", "clang-bit-cast-expression")
PRODUCTION(clang_bit_cast, "clang-bit-cast-expression", "__builtin_bit_cast ( type-id , assignment-expression )")

/* ---- clang Language Extensions, __builtin_convertvector ---- */

PRODUCTION(clang_convertvector, "primary-expression", "clang-convertvector-expression")
PRODUCTION(clang_convertvector, "clang-convertvector-expression", "__builtin_convertvector ( assignment-expression , type-id )")

/* ---- clang Language Extensions, C11 _Atomic in C++ ---- */

PRODUCTION(clang_atomic, "simple-type-specifier", "clang-atomic-type-specifier")
PRODUCTION(clang_atomic, "clang-atomic-type-specifier", "_Atomic ( type-id )")
PRODUCTION(clang_atomic, "decl-specifier", "_Atomic")
PRODUCTION(clang_atomic, "cv-qualifier", "_Atomic")

/* ---- clang Language Extensions, C11 _Thread_local in C++ ---- */

PRODUCTION(clang_c11_thread_local, "storage-class-specifier", "_Thread_local")
PRODUCTION(gnu_thread_local, "storage-class-specifier", "__thread")

/* ---- clang Language Extensions, _BitInt in C++ ---- */

PRODUCTION(clang_deduction_guide_attributes, "deduction-guide", "attribute-specifier-seq explicit-specifier_opt template-name ( parameter-declaration-clause ) -> simple-template-id ;")
PRODUCTION(clang_blocks, "ptr-operator", "^ attribute-specifier-seq_opt cv-qualifier-seq_opt")
PRODUCTION(clang_bit_int, "simple-type-specifier", "clang-bit-int-type-specifier")
PRODUCTION(clang_bit_int, "clang-bit-int-type-specifier", "_BitInt ( constant-expression )")

/* ---- clang Language Extensions, Source location builtins ---- */

PRODUCTION(clang_source_location, "primary-expression", "clang-source-location-expression")
PRODUCTION(clang_source_location, "clang-source-location-expression", "__builtin_source_location ( )")
PRODUCTION(clang_source_location, "clang-source-location-expression", "__builtin_FILE ( )")
PRODUCTION(clang_source_location, "clang-source-location-expression", "__builtin_FILE_NAME ( )")
PRODUCTION(clang_source_location, "clang-source-location-expression", "__builtin_FUNCTION ( )")
PRODUCTION(clang_source_location, "clang-source-location-expression", "__builtin_LINE ( )")
PRODUCTION(clang_source_location, "clang-source-location-expression", "__builtin_COLUMN ( )")

/* ---- [dcl.fct.def.general]/8 predefined names ---- */

PRODUCTION(clang_predefined_names, "primary-expression", "__func__")
PRODUCTION(clang_predefined_names, "primary-expression", "__FUNCTION__")
PRODUCTION(clang_predefined_names, "primary-expression", "__PRETTY_FUNCTION__")

/* ---- clang Language Extensions, Type Trait Primitives ---- */

PRODUCTION(clang_type_traits, "primary-expression", "clang-type-trait-expression")
PRODUCTION(clang_type_traits, "clang-type-trait-expression", "clang-type-trait ( clang-type-trait-argument-list )")
PRODUCTION(clang_type_traits, "clang-type-trait-argument-list", "clang-type-trait-argument")
PRODUCTION(clang_type_traits, "clang-type-trait-argument-list",
           "clang-type-trait-argument-list , clang-type-trait-argument")
PRODUCTION(clang_type_traits, "clang-type-trait-argument", "type-id ..._opt")
PRODUCTION(clang_type_traits, "clang-type-trait-argument", "assignment-expression ..._opt")
PRODUCTION(clang_type_traits, "simple-type-specifier", "clang-type-transformation")
PRODUCTION(clang_type_traits, "clang-type-transformation", "clang-type-transformation-keyword ( type-id )")
TERMINALS(clang_type_traits, "clang-type-trait", "extension-keywords.inc: EXTENSION_KEYWORD(type_trait, ...)")
TERMINALS(clang_type_traits, "clang-type-transformation-keyword",
          "extension-keywords.inc: EXTENSION_KEYWORD(type_transformation, ...)")

/* ---- C++26's = delete ( string-literal ) (P2573R2) before C++26 ---- */

PRODUCTION(clang_deleted_function_message, "function-body", "= delete ( string-literal ) ;")

#undef PRODUCTION
#undef TERMINALS
