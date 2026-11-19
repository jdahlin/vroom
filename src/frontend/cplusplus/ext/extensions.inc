/*
 * The extensions ([intro.compliance]/8: "A conforming implementation may
 * have extensions ..., provided they do not alter the behavior of any
 * well-formed program") the front end has, each with the implementation it
 * comes from and where that one documents it.  Code that implements one is
 * in ext/VENDOR/ where it can be, and asks behavior.has(extension::NAME);
 * its comment cites the extension as [ext:NAME], which
 * bench/cxx-spec-refs.py checks names a row here.  The syntax an extension
 * adds is in grammar.inc, and code that parses it quotes those productions
 * as it quotes the standard's.  Where the code cannot be moved out of the
 * standard's, a comment about the extension in general carries the tag
 * [ext.VENDOR].  A choice the standard leaves to the
 * implementation (implementation-defined or undefined behavior), made as
 * that implementation makes it, is no extension: its tag is [impl.VENDOR].
 *
 * EXTENSION(name, vendor, reference)
 */

/* GNU (GCC's preprocessor and compiler; clang has them too) */
EXTENSION(gnu_line_marker, gnu,
          "GNU cpp manual 9, Preprocessor Output: # linenum filename flags")
EXTENSION(gnu_include_next, gnu, "GNU cpp manual 2.7, Wrapper Headers: #include_next")
EXTENSION(gnu_named_variadic_macro, gnu, "GCC manual 6.21, Variadic Macros: #define f(args...)")
EXTENSION(gnu_comma_paste, gnu,
          "GCC manual 6.21, Variadic Macros: , ## __VA_ARGS__ deletes the comma")
EXTENSION(gnu_ident_directive, gnu, "GNU cpp manual 11, Other Directives: #ident, #sccs")
EXTENSION(gnu_pragmas, gnu,
          "GNU cpp manual 7, Pragmas: GCC poison, GCC system_header, GCC warning, GCC error")
EXTENSION(gnu_push_pop_macro, gnu,
          "GCC manual 6.62.13, Push/Pop Macro Pragmas (Microsoft's, in GCC too)")
EXTENSION(gnu_pragma_message, gnu, "GCC manual 6.62.11, Diagnostic Pragmas: #pragma message")
EXTENSION(gnu_common_predefined_macros, gnu,
          "GNU cpp manual 3.7.2: __COUNTER__, __INCLUDE_LEVEL__, __BASE_FILE__, __TIMESTAMP__")
/* GNU's C++ (doc/PLAN-cxx-parse.md P9; ext/gnu/) */
EXTENSION(gnu_alternate_keywords, gnu,
          "GCC manual 6.48, Alternate Keywords: __inline__, __const__, __signed__, "
          "__volatile__, __asm__, __alignof__ for inline, const, signed, volatile, asm, alignof")
EXTENSION(gnu_attributes, gnu,
          "GCC manual 6.39, Attribute Syntax: __attribute__ ((attribute-list))")
EXTENSION(gnu_library_builtins, gnu,
          "GCC manual 6.63, Other Built-in Functions Provided by GCC: __builtin_ forms "
          "of the ISO C library functions")
EXTENSION(gnu_asm_labels, gnu,
          "GCC manual 6.47.4, Controlling Names Used in Assembler Code: T x asm (\"name\")")
EXTENSION(gnu_restrict, gnu,
          "GCC manual 7.3, Restricting Pointer Aliasing: __restrict__ as a cv-qualifier")
EXTENSION(gnu_extension_keyword, gnu,
          "GCC manual 6.48, Alternate Keywords: __extension__ before a declaration or expression")
EXTENSION(gnu_int128, gnu,
          "GCC manual 6.9, 128-bit Integers: __int128 and unsigned __int128, an extended "
          "integer type ([basic.fundamental]/2)")
EXTENSION(gnu_labels_as_values, gnu,
          "GCC manual 6.3, Labels as Values: &&label and goto *expression;")
EXTENSION(gnu_designated_initializers, gnu,
          "GCC manual 6.29, Designated Initializers: C99's [index] =, .a.b =, a list that "
          "mixes clauses with designators, [first ... last] =, and the obsolete field:")
EXTENSION(gnu_compound_literals, gnu,
          "GCC manual 6.30, Compound Literals: C99's (type-id) { initializer-list } in C++")
EXTENSION(gnu_offsetof, gnu,
          "GCC manual 6.53, Support for offsetof: __builtin_offsetof (type, member), "
          "<cstddef>'s offsetof ([support.types.layout]/1)")

/* clang */
EXTENSION(clang_feature_checks, clang,
          "clang Language Extensions, Feature Checking Macros: __has_feature, __has_builtin, ...")
EXTENSION(clang_has_include_next, clang, "clang Language Extensions: __has_include_next")
EXTENSION(clang_builtin_templates, clang,
          "clang's BuiltinTemplates.td: __builtin_common_type, ...")
EXTENSION(clang_file_name_macro, clang, "clang Language Extensions: __FILE_NAME__")
EXTENSION(clang_flt_eval_method, clang,
          "__FLT_EVAL_METHOD__, as #pragma clang fp eval_method sets it (0 on this target)")
EXTENSION(clang_later_edition_features, clang,
          "a later edition's preprocessor in an earlier one, with a warning: #elifdef, #elifndef, "
          "#warning (C++23), omitted variable arguments (C++20), variadic macros (C++11)")
EXTENSION(clang_import_directive, clang, "Objective-C's #import, an include done once")
EXTENSION(clang_embed, clang,
          "C23 6.10.4 #embed and __has_embed (C++26's [cpp.embed], P1967R14) in C++")
EXTENSION(clang_unicode_white_space, clang,
          "white space beyond [lex.charset]/1's (U+00A0, U+2000, ...), with a warning")

EXTENSION(clang_type_trait_names, clang,
          "clang Language Extensions, Type Trait Primitives: a type trait's keyword not "
          "followed by ( is an identifier (libstdc++ names templates __is_signed, "
          "__make_unsigned, ...)")
EXTENSION(clang_type_traits, clang,
          "clang Language Extensions, Type Trait Primitives: __is_same(T, U), "
          "__remove_cv(T), ... as expressions and type-specifiers")
EXTENSION(clang_nullability, clang,
          "clang Language Extensions, Nullability: _Nonnull, _Nullable, _Nullable_result "
          "and _Null_unspecified as cv-qualifiers, which do not change the type")
EXTENSION(clang_deleted_function_message, clang,
          "C++26's = delete(\"reason\") (P2573R2) in earlier editions, with a warning")
EXTENSION(clang_no_operator_names, clang,
          "-fno-operator-names: and, or, ... are identifiers ([lex.digraph]/2 off)")

/* Microsoft (-fms-extensions) */
EXTENSION(microsoft_pragma_operator, microsoft, "MSVC: __pragma(pp-tokens), the #pragma operator")
EXTENSION(microsoft_operator_name_macros, microsoft,
          "MSVC: and, or, ... can be macro names")
EXTENSION(microsoft_invalid_paste, microsoft,
          "MSVC: a ## that forms no token keeps both, with a warning")
