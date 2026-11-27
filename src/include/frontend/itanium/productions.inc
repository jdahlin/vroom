/*
 * The terminals of the mangling grammar the mangler writes, other than
 * the operators (operators.inc) and the builtin types (builtin-types.inc):
 * the one place each is spelled.  ITANIUM_TERMINAL(name, code), grouped by
 * the section of the ABI whose productions have them; the writer
 * (writer.h) writes them, the manglers name them.  A name starts with its
 * code, so a call shows the bytes it writes (terminal::R_lvalue_reference
 * is R); a leading _ of a code is spelled underscore_.
 *
 * [abi:mangling-structure]:
 *
 *     <mangled-name> ::= _Z <encoding>
 */
ITANIUM_TERMINAL(underscore_Z_mangled_name, "_Z")
/*
 * E ends a <nested-name>, <template-args>, <local-name>'s function scope,
 * <function-type>, an <expr-primary>, a <template-arg> expression and the
 * expressions of a list; _ ends a <template-param>, <discriminator>,
 * <seq-id> and the numbers of several others.
 */
ITANIUM_TERMINAL(E_end, "E")
ITANIUM_TERMINAL(underscore, "_")
/*
 * [abi:mangle.name]:
 *
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *     <unqualified-name> ::= DC <source-name>+ E
 */
ITANIUM_TERMINAL(N_nested_name, "N")
ITANIUM_TERMINAL(DC_structured_binding, "DC")
/*
 * [abi:mangle.abi-tag]:
 *
 *     <abi-tag> ::= B <source-name>
 */
ITANIUM_TERMINAL(B_abi_tag, "B")
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
ITANIUM_TERMINAL(S_substitution, "S")
ITANIUM_TERMINAL(St_std_prefix, "St")
ITANIUM_TERMINAL(Sa_std_allocator, "Sa")
ITANIUM_TERMINAL(Sb_std_basic_string, "Sb")
ITANIUM_TERMINAL(Ss_std_string, "Ss")
ITANIUM_TERMINAL(Si_std_istream, "Si")
ITANIUM_TERMINAL(So_std_ostream, "So")
ITANIUM_TERMINAL(Sd_std_iostream, "Sd")
/*
 * [abi:mangle.number]:
 *
 *     <number> ::= [n] <non-negative decimal integer>
 */
ITANIUM_TERMINAL(n_negative, "n")
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
ITANIUM_TERMINAL(TV_virtual_table, "TV")
ITANIUM_TERMINAL(TT_vtt, "TT")
ITANIUM_TERMINAL(TI_typeinfo, "TI")
ITANIUM_TERMINAL(TS_typeinfo_name, "TS")
ITANIUM_TERMINAL(TC_construction_virtual_table, "TC")
/*
 * [abi:mangling-special-thunks]:
 *
 *     <special-name> ::= T <call-offset> <base encoding>
 *     <call-offset> ::= h <nv-offset> _
 *                   ::= v <v-offset> _
 */
ITANIUM_TERMINAL(T_thunk, "T")
ITANIUM_TERMINAL(h_non_virtual_offset, "h")
ITANIUM_TERMINAL(v_virtual_offset, "v")
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
ITANIUM_TERMINAL(C1_complete_constructor, "C1")
ITANIUM_TERMINAL(C2_base_constructor, "C2")
ITANIUM_TERMINAL(CI1_complete_inheriting_constructor, "CI1")
ITANIUM_TERMINAL(CI2_base_inheriting_constructor, "CI2")
ITANIUM_TERMINAL(D0_deleting_destructor, "D0")
ITANIUM_TERMINAL(D1_complete_destructor, "D1")
ITANIUM_TERMINAL(D2_base_destructor, "D2")
/*
 * [abi:mangling-special-guards]:
 *
 *     <special-name> ::= GV <object name>
 *
 * [abi:mangling-special-temporaries]:
 *
 *     <special-name> ::= GR <object name> _
 */
ITANIUM_TERMINAL(GV_guard_variable, "GV")
ITANIUM_TERMINAL(GR_reference_temporary, "GR")
/*
 * [abi:mangling-operator]:
 *
 *     <operator-name> ::= cv <type>
 *                     ::= li <source-name>
 *
 * (cv the cast in an <expression> too)
 */
ITANIUM_TERMINAL(cv_cast, "cv")
ITANIUM_TERMINAL(li_literal_operator, "li")
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
ITANIUM_TERMINAL(P_pointer, "P")
ITANIUM_TERMINAL(R_lvalue_reference, "R")
ITANIUM_TERMINAL(O_rvalue_reference, "O")
ITANIUM_TERMINAL(C_complex_pair, "C")
ITANIUM_TERMINAL(U_vendor_qualifier, "U")
/*
 * [abi:mangle.qualified-type]:
 *
 *     <CV-qualifiers> ::= [r] [V] [K]
 *     <ref-qualifier> ::= R
 *                     ::= O
 */
ITANIUM_TERMINAL(r_restrict_qualifier, "r")
ITANIUM_TERMINAL(V_volatile_qualifier, "V")
ITANIUM_TERMINAL(K_const_qualifier, "K")
ITANIUM_TERMINAL(R_lvalue_ref_qualifier, "R")
ITANIUM_TERMINAL(O_rvalue_ref_qualifier, "O")
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
ITANIUM_TERMINAL(z_ellipsis, "z")
ITANIUM_TERMINAL(Da_auto_type, "Da")
ITANIUM_TERMINAL(Dc_decltype_auto_type, "Dc")
ITANIUM_TERMINAL(Dk_constrained_auto_type, "Dk")
ITANIUM_TERMINAL(DK_constrained_decltype_auto_type, "DK")
ITANIUM_TERMINAL(DB_bit_int, "DB")
ITANIUM_TERMINAL(DU_unsigned_bit_int, "DU")
ITANIUM_TERMINAL(Dv_vector, "Dv")
ITANIUM_TERMINAL(u_vendor_type, "u")
/*
 * [abi:mangle.function-type]:
 *
 *     <function-type> ::= [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
 *     <exception-spec> ::= Do
 *     <type> ::= Dp <type>
 */
ITANIUM_TERMINAL(F_function_type, "F")
ITANIUM_TERMINAL(Do_non_throwing, "Do")
ITANIUM_TERMINAL(Dp_pack_expansion, "Dp")
/*
 * [abi:mangle.decltype]:
 *
 *     <decltype> ::= Dt <expression> E
 *                ::= DT <expression> E
 */
ITANIUM_TERMINAL(Dt_decltype_entity, "Dt")
ITANIUM_TERMINAL(DT_decltype_expression, "DT")
/*
 * [abi:mangling.named]:
 *
 *     <unnamed-type-name> ::= Ut [ <nonnegative number> ] _
 */
ITANIUM_TERMINAL(Ut_unnamed_type, "Ut")
/*
 * [abi:mangle.array-type]:
 *
 *     <array-type> ::= A [<array bound number>] _ <element type>
 *
 * [abi:mangle.pointer-to-member-type]:
 *
 *     <pointer-to-member-type> ::= M <class type> <member type>
 */
ITANIUM_TERMINAL(A_array_type, "A")
ITANIUM_TERMINAL(M_pointer_to_member, "M")
/*
 * [abi:mangle.template-param]:
 *
 *     <template-param> ::= T_
 *                      ::= T <parameter-2 non-negative number> _
 *
 * and [impl.clang]'s TL of an enclosing level's, and the
 * template-param-decls of a later revision: Ty, Tn, Tp, Tk, Tt.
 */
ITANIUM_TERMINAL(T_template_param, "T")
ITANIUM_TERMINAL(TL_leveled_template_param, "TL")
ITANIUM_TERMINAL(Ty_type_parameter_decl, "Ty")
ITANIUM_TERMINAL(Tn_non_type_parameter_decl, "Tn")
ITANIUM_TERMINAL(Tp_pack_parameter_decl, "Tp")
ITANIUM_TERMINAL(Tk_constrained_parameter_decl, "Tk")
ITANIUM_TERMINAL(Tt_template_parameter_decl, "Tt")
/*
 * [abi:mangle.function-param]:
 *
 *     <function-param> ::= fp <top-level CV-qualifiers> _
 */
ITANIUM_TERMINAL(fp_function_param, "fp")
/*
 * [abi:mangle.template-arg]:
 *
 *     <template-args> ::= I <template-arg>+ E
 *     <template-arg> ::= X <expression> E
 *                    ::= J <template-arg>* E
 *
 * and [impl.clang]'s Q <requires-clause expression> before the E.
 */
ITANIUM_TERMINAL(I_template_args, "I")
ITANIUM_TERMINAL(X_template_arg_expression, "X")
ITANIUM_TERMINAL(J_template_arg_pack, "J")
ITANIUM_TERMINAL(Q_requires_clause, "Q")
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
ITANIUM_TERMINAL(dc_dynamic_cast_, "dc")
ITANIUM_TERMINAL(sc_static_cast_, "sc")
ITANIUM_TERMINAL(cc_const_cast_, "cc")
ITANIUM_TERMINAL(rc_reinterpret_cast_, "rc")
ITANIUM_TERMINAL(tl_braced_cast, "tl")
ITANIUM_TERMINAL(st_sizeof_type, "st")
ITANIUM_TERMINAL(sz_sizeof_expression, "sz")
ITANIUM_TERMINAL(at_alignof_type, "at")
ITANIUM_TERMINAL(az_alignof_expression, "az")
ITANIUM_TERMINAL(dt_member_access, "dt")
ITANIUM_TERMINAL(sZ_sizeof_pack, "sZ")
ITANIUM_TERMINAL(sp_pack_expansion_expression, "sp")
ITANIUM_TERMINAL(gs_global_scope, "gs")
ITANIUM_TERMINAL(sr_unresolved_scope, "sr")
ITANIUM_TERMINAL(L_expr_primary, "L")
ITANIUM_TERMINAL(L_Z_external_name, "L_Z")
ITANIUM_TERMINAL(rq_requires_expression, "rq")
ITANIUM_TERMINAL(X_requirement, "X")
ITANIUM_TERMINAL(N_noexcept_requirement, "N")
ITANIUM_TERMINAL(R_return_type_requirement, "R")
ITANIUM_TERMINAL(so_subobject, "so")
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
ITANIUM_TERMINAL(Z_local_name, "Z")
ITANIUM_TERMINAL(long_discriminator, "__")
ITANIUM_TERMINAL(Ul_closure_type, "Ul")
ITANIUM_TERMINAL(d_default_argument, "d")
ITANIUM_TERMINAL(M_closure_prefix, "M")
/*
 * [impl.clang] internal linkage before a <source-name>, L; the unnamed
 * namespace's name _GLOBAL__N_1; the stand-in of a substituted pack.
 */
ITANIUM_TERMINAL(L_internal_linkage, "L")
ITANIUM_TERMINAL(underscore_SUBSTPACK_substituted_pack, "_SUBSTPACK_")
/* [impl.clang] the vendor qualifier of enable_if(true, ...) is U, then
   clang's spelling of the attribute's name, with an a before its
   <source-name> */
ITANIUM_TERMINAL(a9enable_if_attribute, "a9enable_if")
