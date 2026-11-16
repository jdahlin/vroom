/*
 * The extensions ([intro.compliance]/8: "A conforming implementation may
 * have extensions ..., provided they do not alter the behavior of any
 * well-formed program") the front end has, each with the implementation it
 * comes from and where that one documents it.  Code that implements one is
 * in ext/VENDOR/ where it can be, and asks behavior.has(extension::NAME);
 * where it cannot be moved out of the standard's code, its comment starts
 * with the tag [ext.VENDOR].  A choice the standard leaves to the
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

/* clang */
EXTENSION(clang_feature_checks, clang,
          "clang Language Extensions, Feature Checking Macros: __has_feature, __has_builtin, ...")
EXTENSION(clang_has_include_next, clang, "clang Language Extensions: __has_include_next")
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

/* Microsoft (-fms-extensions) */
EXTENSION(microsoft_operator_name_macros, microsoft,
          "MSVC: and, or, ... can be macro names")
EXTENSION(microsoft_invalid_paste, microsoft,
          "MSVC: a ## that forms no token keeps both, with a warning")
