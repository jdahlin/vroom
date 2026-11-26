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
EXTENSION(gnu_thread_local, gnu,
          "GCC manual, Thread-Local Storage: __thread as a storage-class specifier")
EXTENSION(gnu_attributes, gnu,
          "GCC manual 6.39, Attribute Syntax: __attribute__ ((attribute-list))")
EXTENSION(gnu_library_builtins, gnu,
          "GCC manual 6.63, Other Built-in Functions Provided by GCC: __builtin_ forms "
          "of the ISO C library functions")
EXTENSION(gnu_asm_labels, gnu,
          "GCC manual 6.47.4, Controlling Names Used in Assembler Code: T x asm (\"name\")")
EXTENSION(gnu_asm_statements, gnu,
          "GCC manual 6.47, Using Assembly Language with C: asm statements, basic and "
          "extended (asm volatile (\"...\" : outputs : inputs : clobbers))")
EXTENSION(gnu_restrict, gnu,
          "GCC manual 7.3, Restricting Pointer Aliasing: __restrict__ as a cv-qualifier")
EXTENSION(gnu_extension_keyword, gnu,
          "GCC manual 6.48, Alternate Keywords: __extension__ before a declaration or expression")
EXTENSION(gnu_int128, gnu,
          "GCC manual 6.9, 128-bit Integers: __int128 and unsigned __int128, an extended "
          "integer type ([basic.fundamental]/2)")
EXTENSION(gnu_float128, gnu,
          "GCC manual 6.12, Additional Floating Types: __float128, IEEE binary128")
EXTENSION(clang_float16, clang,
          "clang Language Extensions, Half-Precision Floating Point: _Float16 and __bf16, "
          "types of 16 bits")
EXTENSION(gnu_complex, gnu,
          "GCC manual 6.12, Complex Numbers: _Complex, __complex__ and __complex with float, "
          "double and long double")
EXTENSION(gnu_vector_size, gnu,
          "GCC manual 6.37, Using Vector Instructions through Built-in Functions: "
          "__attribute__((vector_size(N))) on a typedef or variable, a vector of N bytes")
EXTENSION(gnu_zero_length_array, gnu,
          "GCC manual 6.18, Arrays of Length Zero: T[0], which clang's __is_array and "
          "__is_bounded_array do not count as an array")
EXTENSION(gnu_typeof, gnu,
          "GCC manual 6.7, Referring to a Type with typeof: typeof, __typeof and __typeof__ "
          "(expression) or (type-id), a type-specifier (glibc's <math.h> casts through it)")
EXTENSION(gnu_alignof_expression, gnu,
          "GCC manual 6.46, Inquiring on Alignment of Types or Variables: alignof and "
          "__alignof__ of an expression, the alignment of its type")
EXTENSION(gnu_labels_as_values, gnu,
          "GCC manual 6.3, Labels as Values: &&label and goto *expression;")
EXTENSION(gnu_conditional_omitted, gnu,
          "GCC manual 6.8, Conditionals with Omitted Operands: x ? : y, x evaluated once")
EXTENSION(gnu_designated_initializers, gnu,
          "GCC manual 6.29, Designated Initializers: C99's [index] =, .a.b =, a list that "
          "mixes clauses with designators, [first ... last] =, and the obsolete field:")
EXTENSION(gnu_compound_literals, gnu,
          "GCC manual 6.30, Compound Literals: C99's (type-id) { initializer-list } in C++")
EXTENSION(gnu_offsetof, gnu,
          "GCC manual 6.53, Support for offsetof: __builtin_offsetof (type, member), "
          "<cstddef>'s offsetof ([support.types.layout]/1)")
EXTENSION(gnu_va_arg, gnu,
          "GCC manual 6.55 (Variable Arguments): __builtin_va_arg (va_list, type), "
          "<cstdarg>'s va_arg ([cstdarg.syn])")
EXTENSION(gnu_statement_expressions, gnu,
          "GCC manual 6.1, Statements and Declarations in Expressions: ({ ... })")

/* clang */
EXTENSION(clang_feature_checks, clang,
          "clang Language Extensions, Feature Checking Macros: __has_feature, __has_builtin, ...")
EXTENSION(clang_has_include_next, clang, "clang Language Extensions: __has_include_next")
EXTENSION(clang_builtin_templates, clang,
          "clang's BuiltinTemplates.td: __builtin_common_type, ...")
EXTENSION(clang_builtin_functions, clang,
          "clang Language Extensions, Builtin Functions: __builtin_NAME functions that are not "
          "C library functions, such as __builtin_coro_resume")
EXTENSION(clang_file_name_macro, clang, "clang Language Extensions: __FILE_NAME__")
EXTENSION(clang_flt_eval_method, clang,
          "__FLT_EVAL_METHOD__, as #pragma clang fp eval_method sets it (0 on this target)")
EXTENSION(clang_later_edition_features, clang,
          "a later edition's preprocessor in an earlier one, with a warning: #elifdef, #elifndef, "
          "#warning (C++23), omitted variable arguments (C++20), variadic macros (C++11); "
          "an alias-declaration as an init-statement (P2360R0, C++23); explicit ( constant-expression ) "
          "(P0892R2, C++20)")
EXTENSION(clang_import_directive, clang, "Objective-C's #import, an include done once")
EXTENSION(clang_embed, clang,
          "C23 6.10.4 #embed and __has_embed (C++26's [cpp.embed], P1967R14) in C++")
EXTENSION(clang_diagnose_if, clang,
          "clang Attributes, diagnose_if: an error or warning at a call whose arguments make the condition true")
EXTENSION(clang_initializer_list, clang,
          "the backing array of a std::initializer_list evaluated in constant expressions, as clang does")
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
EXTENSION(clang_atomic, clang,
          "clang Language Extensions, C11 _Atomic in C++: _Atomic(T) a type with C11's "
          "atomic type's layout (C23 6.2.5)")
EXTENSION(clang_c11_keywords, clang,
          "clang Language Extensions, C11 keywords in C++: _Static_assert, _Alignas and "
          "_Alignof, other spellings of static_assert, alignas and alignof with the same "
          "meaning (clang: -Wc11-extensions)")
EXTENSION(clang_c11_thread_local, clang,
          "clang Language Extensions, C11 _Thread_local in C++: a storage-class-specifier with "
          "C11's rules (constant initialization, trivial destruction, static or namespace "
          "scope), accessed directly, without thread_local's wrapper functions")
EXTENSION(clang_availability, clang,
          "clang Attributes, availability: availability(platform, introduced=version, ...), "
          "a version a pp-number such as 10.12.1")
EXTENSION(clang_bit_int, clang,
          "clang Language Extensions, _BitInt (C23 6.2.5) in C++: _BitInt(N) a type-specifier")
EXTENSION(clang_ext_vector_type, clang,
          "clang Language Extensions, Vectors and Extended Vectors: "
          "__attribute__((ext_vector_type(N))) on an alias-declaration, a vector of N "
          "elements (libc++'s __algorithm/simd_utils.h)")
EXTENSION(clang_deduction_guide_attributes, clang,
          "an attribute-specifier-seq before a deduction-guide (libc++'s "
          "_LIBCPP_CTAD_SUPPORTED_FOR_TYPE), which the attributes do not change")
EXTENSION(clang_blocks, clang,
          "clang Language Extensions, Blocks: ^ a ptr-operator, a block pointer to a "
          "function type (Darwin's SDK declares functions taking one under __BLOCKS__)")
EXTENSION(clang_source_location, clang,
          "clang Language Extensions, Source location builtins: __builtin_source_location(), "
          "std::source_location::current's primitive, and __builtin_FILE(), __builtin_FILE_NAME(), "
          "__builtin_FUNCTION(), __builtin_LINE() and __builtin_COLUMN(), which in a default "
          "argument are the call's")
EXTENSION(clang_predefined_names, clang,
          "[dcl.fct.def.general]/8's __func__ (a keyword here, as clang lexes it) and "
          "clang's __FUNCTION__ and __PRETTY_FUNCTION__: the function's name as a "
          "static const char array")
EXTENSION(clang_bit_cast, clang,
          "clang Language Extensions: __builtin_bit_cast (type, expression), "
          "std::bit_cast ([bit.cast]) as a primitive")
EXTENSION(clang_datasizeof, clang,
          "clang Language Extensions: __datasizeof (type-id) and __datasizeof expression, "
          "the size without the tail padding of a class ([abi:layout]'s data size)")
EXTENSION(clang_vla, clang,
          "clang Language Extensions: variable length arrays in C++ (C99's, GCC manual 6.20 "
          "Arrays of Variable Length): an automatic array whose bound is a run-time value")
EXTENSION(clang_convertvector, clang,
          "clang Language Extensions, Vector Operations: __builtin_convertvector (vector, type), "
          "each element converted to the vector type's element type")
EXTENSION(clang_lambda_template_parameters, clang,
          "clang: a template-parameter-list after the lambda-introducer (P0428R2) is "
          "accepted before C++20 (libc++'s <__algorithm/simd_utils.h> uses it in C++17)")
EXTENSION(clang_lambda_attributes, clang,
          "clang: an attribute-specifier-seq before the lambda-declarator or compound-statement "
          "(P2173R1, C++23) is accepted in earlier editions (libc++'s <format> uses it)")

/* Microsoft (-fms-extensions) */
EXTENSION(microsoft_declspec, microsoft,
          "MSVC: __declspec(attribute-list) among the declaration specifiers (clang's "
          "ParseMicrosoftDeclSpecs)")
EXTENSION(microsoft_pragma_operator, microsoft, "MSVC: __pragma(pp-tokens), the #pragma operator")
EXTENSION(microsoft_operator_name_macros, microsoft,
          "MSVC: and, or, ... can be macro names")
EXTENSION(microsoft_invalid_paste, microsoft,
          "MSVC: a ## that forms no token keeps both, with a warning")
