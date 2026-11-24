/*
 * The changes between editions that the front end asks about, each with
 * the edition that brought it, the edition that removed it (never: none), and
 * where it is recorded: Annex C ([diff]) when it changed the meaning of an
 * earlier edition's program, else the paper that added it.
 *
 * LANGUAGE_FEATURE(name, since, until, reference)
 */

/* C++11 */
LANGUAGE_FEATURE(unicode_and_raw_string_literals, cplusplus_11, never,
                 "[diff.cpp03.lex]/1: New kinds of string-literals")
LANGUAGE_FEATURE(user_defined_literals, cplusplus_11, never,
                 "[diff.cpp03.lex]/2: User-defined literal string support")
LANGUAGE_FEATURE(cplusplus_11_keywords, cplusplus_11, never, "[diff.cpp03.lex]/3: New keywords")
LANGUAGE_FEATURE(less_colon_colon_is_less, cplusplus_11, never,
                 "[lex.pptoken]/3.2: <:: not followed by : or > is < ::, new in C++11")
LANGUAGE_FEATURE(universal_character_names_for_basic_characters_in_literals, cplusplus_11, never,
                 "[lex.charset]/2: allowed inside literals; C++03 forbade them everywhere")
/* C++14 */
LANGUAGE_FEATURE(aggregates_with_default_member_initializers, cplusplus_14, never,
                 "[diff.cpp11.dcl.dcl]/2: classes with default member initializers can "
                 "be aggregates")
LANGUAGE_FEATURE(digit_separators, cplusplus_14, never,
                 "[diff.cpp11.lex]/1: pp-number can contain one or more single quotes")
LANGUAGE_FEATURE(binary_literals, cplusplus_14, never, "N3472: binary-literal")
LANGUAGE_FEATURE(library_ud_suffixes, cplusplus_14, never,
                 "N3642, N3660: the library's s, h, min, ms, us, ns, i, il, if")
/* C++17 */
LANGUAGE_FEATURE(trigraphs, cplusplus_98, cplusplus_17,
                 "[diff.cpp14.lex]/1: Removal of trigraph support as a required feature")
LANGUAGE_FEATURE(hexadecimal_floating_point_literals, cplusplus_17, never,
                 "[diff.cpp14.lex]/2: pp-number can contain p sign and P sign")
LANGUAGE_FEATURE(utf8_character_literals, cplusplus_17, never, "N4267: u8 character-literal")
/* C++20 */
LANGUAGE_FEATURE(cplusplus_20_keywords, cplusplus_20, never, "[diff.cpp17.lex]/3: New keywords")
LANGUAGE_FEATURE(three_way_comparison_operator, cplusplus_20, never, "[diff.cpp17.lex]/4: New operator <=>")
LANGUAGE_FEATURE(char8_t_type, cplusplus_20, never,
                 "[diff.cpp17.lex]/5: Type of UTF-8 string and character literals")
LANGUAGE_FEATURE(chrono_ud_suffixes, cplusplus_20, never, "P0355R7: the library's d and y")
/* C++23 */
LANGUAGE_FEATURE(size_t_suffix, cplusplus_23, never, "P0330R8: literal suffix for size_t")
LANGUAGE_FEATURE(named_universal_character_names, cplusplus_23, never,
                 "P2071R2: named universal character escapes")
LANGUAGE_FEATURE(delimited_escape_sequences, cplusplus_23, never, "P2290R3: delimited escape sequences")
/* C++26 */
LANGUAGE_FEATURE(raw_string_delimiter_dollar_at_grave, cplusplus_26, never,
                 "P2558R2: $ @ ` in the basic character set")
LANGUAGE_FEATURE(deleted_function_message, cplusplus_26, never,
                 "P2573R2: = delete(\"should have a reason\")")
/* [dcl.spec] */
LANGUAGE_FEATURE(auto_type_specifier, cplusplus_11, never,
                 "[diff.cpp03.dcl.dcl]/1: Remove auto as a storage class specifier")
LANGUAGE_FEATURE(return_type_deduction, cplusplus_14, never,
                 "N3638: a function's return type deduced; decltype(auto)")
/* [dcl.decl] */
LANGUAGE_FEATURE(rvalue_references, cplusplus_11, never, "N2118: the ptr-operator &&")
/* [dcl.init] */
LANGUAGE_FEATURE(list_initialization, cplusplus_11, never,
                 "N2672: a braced-init-list outside = { }")
LANGUAGE_FEATURE(aggregates_without_user_declared_constructors, cplusplus_20, never,
                 "[diff.cpp17.dcl.dcl]/3: a class with user-declared constructors is "
                 "never an aggregate")
LANGUAGE_FEATURE(designated_initializers, cplusplus_20, never,
                 "P0329R4: designated-initializer-list")
/* [dcl.struct.bind], [dcl.fct.spec] */
LANGUAGE_FEATURE(structured_bindings, cplusplus_17, never, "P0217R3: structured bindings")
LANGUAGE_FEATURE(aggregates_with_bases, cplusplus_17, never,
                 "[diff.cpp14.dcl.dcl]/4: an aggregate may have base classes")
LANGUAGE_FEATURE(conditional_explicit, cplusplus_20, never,
                 "P0892R2: explicit ( constant-expression )")
/* [class] */
LANGUAGE_FEATURE(default_member_initializers, cplusplus_11, never,
                 "N2756: a brace-or-equal-initializer of a non-static data member")
LANGUAGE_FEATURE(override_control, cplusplus_11, never,
                 "N3272: the virt-specifiers override and final, class-virt-specifier final")
LANGUAGE_FEATURE(bit_field_default_member_initializers, cplusplus_20, never,
                 "P0683R1: a default member initializer of a bit-field")
/* [expr.prim.lambda] */
LANGUAGE_FEATURE(lambda_expressions, cplusplus_11, never, "N2927: lambda-expression")
LANGUAGE_FEATURE(init_captures, cplusplus_14, never, "N3648: init-capture")
LANGUAGE_FEATURE(constexpr_lambdas, cplusplus_17, never, "P0170R1: constexpr in a lambda-declarator")
LANGUAGE_FEATURE(star_this_capture, cplusplus_17, never, "P0018R3: the simple-capture * this")
/* [except.spec] */
LANGUAGE_FEATURE(dynamic_exception_specifications, cplusplus_98, cplusplus_17,
                 "[diff.cpp14.except]/1: Remove dynamic exception specifications")
LANGUAGE_FEATURE(exception_specification_in_type, cplusplus_17, never,
                 "P0012R1: exception specifications part of the type system")
/* [temp.names] */
LANGUAGE_FEATURE(function_names_before_less, cplusplus_20, never,
                 "P0846R0: an unqualified-id that names functions before < is a template-name")
/* [stmt] */
LANGUAGE_FEATURE(range_based_for, cplusplus_11, never, "N2930: the range-based for statement")
LANGUAGE_FEATURE(selection_init_statement, cplusplus_17, never,
                 "P0305R1: an init-statement in if and switch")
LANGUAGE_FEATURE(constexpr_if, cplusplus_17, never, "P0292R2: if constexpr")
LANGUAGE_FEATURE(relaxed_constexpr, cplusplus_14, never,
                 "N3652: a constexpr function's body may have loops, expression statements and a void return type")
LANGUAGE_FEATURE(constexpr_without_constant_result, cplusplus_23, never,
                 "P2448R2: a constexpr function need not be able to produce a constant expression")
LANGUAGE_FEATURE(constexpr_destructors, cplusplus_20, never,
                 "P0784R7: a destructor may be constexpr")
LANGUAGE_FEATURE(non_literal_constexpr_variables, cplusplus_23, never,
                 "P2242R3: a constexpr function may define a variable of non-literal type")
LANGUAGE_FEATURE(unknown_references_in_constant_expressions, cplusplus_23, never,
                 "P2280R4: a reference whose referent is unknown may be named in a constant expression")
LANGUAGE_FEATURE(range_for_init_statement, cplusplus_20, never,
                 "P0614R1: an init-statement in the range-based for statement")
LANGUAGE_FEATURE(labels_anywhere, cplusplus_23, never,
                 "P2324R2: a label at the end of a compound-statement")
/* [expr] */
LANGUAGE_FEATURE(bool_increment, cplusplus_98, cplusplus_17,
                 "[diff.cpp14.expr]/1: Remove increment operator with bool operand")
LANGUAGE_FEATURE(modular_left_shift, cplusplus_20, never,
                 "P1236R1: signed integers are two's complement; E1 << E2 is E1 x 2^E2 modulo 2^N")
LANGUAGE_FEATURE(multidimensional_subscript, cplusplus_23, never,
                 "P2128R6: postfix-expression [ expression-list_opt ]")
LANGUAGE_FEATURE(sized_deallocation, cplusplus_14, never,
                 "N3778: operator delete(void*, std::size_t) ([basic.stc.dynamic]/2)")
LANGUAGE_FEATURE(aligned_allocation, cplusplus_17, never,
                 "P0035R4: operator new(std::size_t, std::align_val_t) for new-extended alignment ([expr.new]/19)")
/* [cpp] */
LANGUAGE_FEATURE(long_long, cplusplus_11, never, "N1811: long long, in #if too ([cpp.cond]/12)")
LANGUAGE_FEATURE(variadic_macros, cplusplus_11, never,
                 "N1653: C99's preprocessor: variadic macros, empty macro arguments")
LANGUAGE_FEATURE(modules, cplusplus_20, never,
                 "[diff.cpp17.lex]/1: New identifiers with special meaning (module, import)")
LANGUAGE_FEATURE(va_opt, cplusplus_20, never, "P0306R4, P1042R1: __VA_OPT__ ([cpp.subst])")
LANGUAGE_FEATURE(variadic_arguments_may_be_omitted, cplusplus_20, never,
                 "[cpp.replace]/5: at least as many arguments as parameters excluding the ... (P1042R1)")
LANGUAGE_FEATURE(warning_directive, cplusplus_23, never, "P2437R1: #warning ([cpp.error])")
LANGUAGE_FEATURE(elifdef_directives, cplusplus_23, never, "P2334R1: #elifdef and #elifndef ([cpp.cond])")
/* templates (P7 of doc/PLAN-cxx-parse.md) */
LANGUAGE_FEATURE(right_angle_brackets, cplusplus_11, never,
                 "[diff.cpp03.temp]/1: >> ends two template-argument-lists (N1757)")
LANGUAGE_FEATURE(variadic_templates, cplusplus_11, never, "N2242: parameter packs and pack expansions")
LANGUAGE_FEATURE(alias_templates, cplusplus_11, never, "N2258: alias templates")
LANGUAGE_FEATURE(extern_templates, cplusplus_11, never, "N1987: extern template")
LANGUAGE_FEATURE(variable_templates, cplusplus_14, never, "N3651: variable templates")
LANGUAGE_FEATURE(fold_expressions, cplusplus_17, never, "N4295: fold-expression")
LANGUAGE_FEATURE(template_auto_parameters, cplusplus_17, never,
                 "P0127R2: a template-parameter of placeholder type")
LANGUAGE_FEATURE(typename_template_template_parameters, cplusplus_17, never,
                 "N4051: typename in a template template parameter")
LANGUAGE_FEATURE(deduction_guides, cplusplus_17, never, "P0091R3: deduction-guide")
LANGUAGE_FEATURE(concepts, cplusplus_20, never,
                 "P0734R0: concept-definition, requires-clause, requires-expression")
LANGUAGE_FEATURE(abbreviated_function_templates, cplusplus_20, never,
                 "P1141R2: placeholders with a type-constraint, abbreviated function templates")
LANGUAGE_FEATURE(lambda_template_parameters, cplusplus_20, never,
                 "P0428R2: a template-parameter-list after the lambda-introducer")
LANGUAGE_FEATURE(implicit_typename, cplusplus_20, never,
                 "P0634R3: [temp.res]/5, contexts where a qualified-id names a type")
LANGUAGE_FEATURE(default_constructible_lambdas, cplusplus_20, never,
                 "P0624R2: a closure type without a lambda-capture has a "
                 "default constructor and copy assignment operator")
LANGUAGE_FEATURE(init_capture_packs, cplusplus_20, never,
                 "P0780R2: an init-capture pack, ...identifier initializer")
LANGUAGE_FEATURE(constant_cast_from_void_pointer, cplusplus_26, never,
                 "P2738R1: a cast from cv void* to a pointer to object is allowed in a "
                 "constant expression")
