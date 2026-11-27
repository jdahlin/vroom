/*
 * The terminals of the mangling grammar the mangler writes, other than
 * the operators (operators.inc) and the builtin types (builtin-types.inc):
 * the one place each is spelled.  ITANIUM_TERMINAL(name, code), grouped by
 * the section of the ABI whose productions have them; the encoder
 * (encoder.cpp) writes them, the builder names them.
 *
 * [abi:mangling-structure]:
 *
 *     <mangled-name> ::= _Z <encoding>
 */
ITANIUM_TERMINAL(mangled_name, "_Z")
/*
 * E ends a <nested-name>, <template-args>, <local-name>'s function scope,
 * <function-type>, an <expr-primary>, a <template-arg> expression and the
 * expressions of a list; _ ends a <template-param>, <discriminator>,
 * <seq-id> and the numbers of several others.
 */
ITANIUM_TERMINAL(end, "E")
ITANIUM_TERMINAL(underscore, "_")
/*
 * [abi:mangle.name]:
 *
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *     <unqualified-name> ::= DC <source-name>+ E
 */
ITANIUM_TERMINAL(nested_name, "N")
ITANIUM_TERMINAL(structured_binding, "DC")
/*
 * [abi:mangle.abi-tag]:
 *
 *     <abi-tag> ::= B <source-name>
 */
ITANIUM_TERMINAL(abi_tag, "B")
/*
 * [abi:mangling-compression]:
 *
 *     <substitution> ::= S <seq-id> _
 *                    ::= S_
 *                    ::= St
 *                    ::= Sa
 *                    ::= Sb
 *                    ::= Ss
 *                    ::= Si
 *                    ::= So
 *                    ::= Sd
 */
ITANIUM_TERMINAL(substitution, "S")
ITANIUM_TERMINAL(std_prefix, "St")
ITANIUM_TERMINAL(std_allocator, "Sa")
ITANIUM_TERMINAL(std_basic_string, "Sb")
ITANIUM_TERMINAL(std_string, "Ss")
ITANIUM_TERMINAL(std_istream, "Si")
ITANIUM_TERMINAL(std_ostream, "So")
ITANIUM_TERMINAL(std_iostream, "Sd")
/*
 * [abi:mangle.number]:
 *
 *     <number> ::= [n] <non-negative decimal integer>
 */
ITANIUM_TERMINAL(negative, "n")
/*
 * [abi:mangling-special-vtables]:
 *
 *     <special-name> ::= TV <type>
 *                    ::= TT <type>
 *                    ::= TI <type>
 *                    ::= TS <type>
 *
 * and [impl.clang]'s construction virtual table, TC <type> <offset> _
 * <base type>.
 */
ITANIUM_TERMINAL(virtual_table, "TV")
ITANIUM_TERMINAL(vtt, "TT")
ITANIUM_TERMINAL(typeinfo, "TI")
ITANIUM_TERMINAL(typeinfo_name, "TS")
ITANIUM_TERMINAL(construction_virtual_table, "TC")
/*
 * [abi:mangling-special-thunks]:
 *
 *     <special-name> ::= T <call-offset> <base encoding>
 *     <call-offset> ::= h <nv-offset> _
 *                   ::= v <v-offset> _
 */
ITANIUM_TERMINAL(thunk, "T")
ITANIUM_TERMINAL(non_virtual_offset, "h")
ITANIUM_TERMINAL(virtual_offset, "v")
/*
 * [abi:mangling-special-ctor-dtor]:
 *
 *     <ctor-dtor-name> ::= C1
 *                      ::= C2
 *                      ::= CI1 <base class type>
 *                      ::= CI2 <base class type>
 *                      ::= D0
 *                      ::= D1
 *                      ::= D2
 */
ITANIUM_TERMINAL(complete_constructor, "C1")
ITANIUM_TERMINAL(base_constructor, "C2")
ITANIUM_TERMINAL(complete_inheriting_constructor, "CI1")
ITANIUM_TERMINAL(base_inheriting_constructor, "CI2")
ITANIUM_TERMINAL(deleting_destructor, "D0")
ITANIUM_TERMINAL(complete_destructor, "D1")
ITANIUM_TERMINAL(base_destructor, "D2")
/*
 * [abi:mangling-special-guards]:
 *
 *     <special-name> ::= GV <object name>
 *
 * [abi:mangling-special-temporaries]:
 *
 *     <special-name> ::= GR <object name> _
 */
ITANIUM_TERMINAL(guard_variable, "GV")
ITANIUM_TERMINAL(reference_temporary, "GR")
/*
 * [abi:mangling-operator]:
 *
 *     <operator-name> ::= cv <type>
 *                     ::= li <source-name>
 *
 * (cv the cast in an <expression> too)
 */
ITANIUM_TERMINAL(cast, "cv")
ITANIUM_TERMINAL(literal_operator, "li")
/*
 * [abi:mangling-type]:
 *
 *     <type> ::= P <type>
 *            ::= R <type>
 *            ::= O <type>
 *            ::= C <type>
 *
 * and the vendor extended type qualifier, U <source-name>.
 */
ITANIUM_TERMINAL(pointer, "P")
ITANIUM_TERMINAL(lvalue_reference, "R")
ITANIUM_TERMINAL(rvalue_reference, "O")
ITANIUM_TERMINAL(complex_pair, "C")
ITANIUM_TERMINAL(vendor_qualifier, "U")
/*
 * [abi:mangle.qualified-type]:
 *
 *     <CV-qualifiers> ::= [r] [V] [K]
 *     <ref-qualifier> ::= R
 *                     ::= O
 */
ITANIUM_TERMINAL(restrict_qualifier, "r")
ITANIUM_TERMINAL(volatile_qualifier, "V")
ITANIUM_TERMINAL(const_qualifier, "K")
ITANIUM_TERMINAL(lvalue_ref_qualifier, "R")
ITANIUM_TERMINAL(rvalue_ref_qualifier, "O")
/*
 * [abi:mangling-builtin]:
 *
 *     <builtin-type> ::= z
 *                    ::= Da
 *                    ::= Dc
 *                    ::= DB <number> _
 *                    ::= DU <number> _
 *                    ::= u <source-name> [<template-args>]
 *
 * and [impl.clang]'s Dk and DK of a constrained auto and decltype(auto),
 * Dv of a vector.
 */
ITANIUM_TERMINAL(ellipsis, "z")
ITANIUM_TERMINAL(auto_type, "Da")
ITANIUM_TERMINAL(decltype_auto_type, "Dc")
ITANIUM_TERMINAL(constrained_auto_type, "Dk")
ITANIUM_TERMINAL(constrained_decltype_auto_type, "DK")
ITANIUM_TERMINAL(bit_int, "DB")
ITANIUM_TERMINAL(unsigned_bit_int, "DU")
ITANIUM_TERMINAL(vector, "Dv")
ITANIUM_TERMINAL(vendor_type, "u")
/*
 * [abi:mangle.function-type]:
 *
 *     <function-type> ::= [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
 *     <exception-spec> ::= Do
 *     <type> ::= Dp <type>
 */
ITANIUM_TERMINAL(function_type, "F")
ITANIUM_TERMINAL(non_throwing, "Do")
ITANIUM_TERMINAL(pack_expansion, "Dp")
/*
 * [abi:mangle.decltype]:
 *
 *     <decltype> ::= Dt <expression> E
 *                ::= DT <expression> E
 */
ITANIUM_TERMINAL(decltype_entity, "Dt")
ITANIUM_TERMINAL(decltype_expression, "DT")
/*
 * [abi:mangling.named]:
 *
 *     <unnamed-type-name> ::= Ut [ <nonnegative number> ] _
 */
ITANIUM_TERMINAL(unnamed_type, "Ut")
/*
 * [abi:mangle.array-type]:
 *
 *     <array-type> ::= A [<array bound number>] _ <element type>
 *
 * [abi:mangle.pointer-to-member-type]:
 *
 *     <pointer-to-member-type> ::= M <class type> <member type>
 */
ITANIUM_TERMINAL(array_type, "A")
ITANIUM_TERMINAL(pointer_to_member, "M")
/*
 * [abi:mangle.template-param]:
 *
 *     <template-param> ::= T_
 *                      ::= T <parameter-2 non-negative number> _
 *
 * and [impl.clang]'s TL of an enclosing level's, and the
 * template-param-decls of a later revision: Ty, Tn, Tp, Tk, Tt.
 */
ITANIUM_TERMINAL(template_param, "T")
ITANIUM_TERMINAL(leveled_template_param, "TL")
ITANIUM_TERMINAL(type_parameter_decl, "Ty")
ITANIUM_TERMINAL(non_type_parameter_decl, "Tn")
ITANIUM_TERMINAL(pack_parameter_decl, "Tp")
ITANIUM_TERMINAL(constrained_parameter_decl, "Tk")
ITANIUM_TERMINAL(template_parameter_decl, "Tt")
/*
 * [abi:mangle.function-param]:
 *
 *     <function-param> ::= fp <top-level CV-qualifiers> _
 */
ITANIUM_TERMINAL(function_param, "fp")
/*
 * [abi:mangle.template-arg]:
 *
 *     <template-args> ::= I <template-arg>+ E
 *     <template-arg> ::= X <expression> E
 *                    ::= J <template-arg>* E
 *
 * and [impl.clang]'s Q <requires-clause expression> before the E.
 */
ITANIUM_TERMINAL(template_args, "I")
ITANIUM_TERMINAL(template_arg_expression, "X")
ITANIUM_TERMINAL(template_arg_pack, "J")
ITANIUM_TERMINAL(requires_clause, "Q")
/*
 * [abi:expressions]:
 *
 *     <expression> ::= cv <type> _ <expression>* E
 *                  ::= dc <type> <expression>
 *                  ::= sc <type> <expression>
 *                  ::= cc <type> <expression>
 *                  ::= rc <type> <expression>
 *                  ::= tl <type> <braced-expression>* E
 *                  ::= st <type>
 *                  ::= sz <expression>
 *                  ::= at <type>
 *                  ::= az <expression>
 *                  ::= dt <expression> <unresolved-name>
 *                  ::= sZ <template-param>
 *                  ::= sp <expression>
 *                  ::= u <source-name> <template-arg>* E
 *     <unresolved-name> ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
 *     <expr-primary> ::= L <type> <value number> E
 *                    ::= L _Z <encoding> E
 *
 * and the later revision's rq of a requires-expression, X of its
 * requirement, so of a subobject (clang's spelling of each).
 */
ITANIUM_TERMINAL(dynamic_cast_, "dc")
ITANIUM_TERMINAL(static_cast_, "sc")
ITANIUM_TERMINAL(const_cast_, "cc")
ITANIUM_TERMINAL(reinterpret_cast_, "rc")
ITANIUM_TERMINAL(braced_cast, "tl")
ITANIUM_TERMINAL(sizeof_type, "st")
ITANIUM_TERMINAL(sizeof_expression, "sz")
ITANIUM_TERMINAL(alignof_type, "at")
ITANIUM_TERMINAL(alignof_expression, "az")
ITANIUM_TERMINAL(member_access, "dt")
ITANIUM_TERMINAL(sizeof_pack, "sZ")
ITANIUM_TERMINAL(pack_expansion_expression, "sp")
ITANIUM_TERMINAL(global_scope, "gs")
ITANIUM_TERMINAL(unresolved_scope, "sr")
ITANIUM_TERMINAL(expr_primary, "L")
ITANIUM_TERMINAL(external_name, "L_Z")
ITANIUM_TERMINAL(requires_expression, "rq")
ITANIUM_TERMINAL(requirement, "X")
ITANIUM_TERMINAL(noexcept_requirement, "N")
ITANIUM_TERMINAL(return_type_requirement, "R")
ITANIUM_TERMINAL(subobject, "so")
/*
 * [abi:mangling-scope]:
 *
 *     <local-name> ::= Z <function encoding> E <entity name> [<discriminator>]
 *     <discriminator> ::= _ <non-negative number>
 *
 * [abi:closure-types]:
 *
 *     <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
 *     <local-name> ::= Z <function encoding> Ed [ <parameter number> ] _ <entity name>
 *     <closure-prefix> ::= [ <prefix> ] <variable or member unqualified-name> M
 */
ITANIUM_TERMINAL(local_name, "Z")
ITANIUM_TERMINAL(long_discriminator, "__")
ITANIUM_TERMINAL(closure_type, "Ul")
ITANIUM_TERMINAL(default_argument, "d")
ITANIUM_TERMINAL(closure_prefix, "M")
/*
 * [impl.clang] internal linkage before a <source-name>, L; the unnamed
 * namespace's name _GLOBAL__N_1; the stand-in of a substituted pack.
 */
ITANIUM_TERMINAL(internal_linkage, "L")
ITANIUM_TERMINAL(substituted_pack, "_SUBSTPACK_")
/* [impl.clang] the vendor qualifier of enable_if(true, ...) is U, then
   clang's spelling of the attribute's name, with an a before its
   <source-name> */
ITANIUM_TERMINAL(enable_if_attribute, "a9enable_if")
