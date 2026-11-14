#ifndef CXX_DECLARATION_H
#define CXX_DECLARATION_H
/*
 * declaration.c: C++ declarations (cxx-internal.h): namespaces, using,
 * templates, classes, enums, decl-specifiers and declarators.
 *
 * Member function bodies are parsed once the outermost class is complete
 * (they may name members declared after them), as clang does; default
 * member initializers and default arguments are parsed in place.
 */
#include "../cxx-internal.h"

/* ---- tentative parsing ------------------------------------------------------------------- */

enum { DS_TYPE = 1, DS_TYPEDEF = 2, DS_FRIEND = 4, DS_MODIFIER = 8, DS_CONSTEXPR = 16, DS_DEFINED_CLASS = 32, DS_STATIC = 64,
       DS_VIRTUAL = 128, DS_EXPLICIT = 256, DS_EXPLICIT_MAYBE = 512, DS_EXTERN_SEEN = 1024, DS_INLINE = 2048, DS_MUTABLE = 4096 };

/* a suffix of a declarator: [n] or (parameters), applied innermost last */
typedef struct cxx_suffix { qty trailing; uint32_t pack; uint8_t is_const, is_noexcept; uint8_t is_function, variadic; int64_t n; qty *params; uint32_t nparams; cxx_param *named; uint32_t nnamed, ndefaults; } cxx_suffix;

/* the functions of declaration/, by file */

/* tokens.c */
VPRIV bool cxx_lookahead(cxx *c, void (*fn)(cxx *));
VPRIV uint32_t cxx_alignment_argument(cxx *c);
VPRIV void cxx_attribute_scan(cxx *c, uint32_t b, uint32_t e);
VPRIV bool cxx_tokens_have_kw(cxx *c, uint32_t b, uint32_t e, int kw);
VPRIV bool cxx_word_scan(cxx *c, uint32_t b, uint32_t e, const char *w);
VPRIV uint32_t cxx_abi_tag_scan(cxx *c, uint32_t b, uint32_t e);
VPRIV bool cxx_class_key_unnamed(cxx *c);
VPRIV void cxx_attributes_skip(cxx *c);

/* name.c */
VPRIV bool cxx_entity_is_type(const cxx_entity *e);
VPRIV bool cxx_entity_is_template(const cxx_entity *e);
VPRIV qty cxx_type_drop_quals(cxx *c, qty t, unsigned drop);
VPRIV qty cxx_type_transform(cxx *c, const char *n, size_t len, qty t);
VPRIV cxx_entity *cxx_name_lookup(cxx *c, cxx_scope *qualifier, bool dependent, uint32_t id);
VPRIV cxx_entity *cxx_entity_prefer(cxx_entity *e, int kind);
VPRIV cxx_entity *cxx_entity_template(cxx_entity *e);
VPRIV cxx_entity *cxx_nested_name_parse(cxx *c, cxx_scope **qualifier, bool *dependent);
VPRIV bool cxx_ends_argument(cxx *c, uint32_t ahead);
VPRIV void cxx_template_argument_parse(cxx *c, cxx_targ *out);
VPRIV void cxx_template_arguments_parse(cxx *c);

/* specifier.c */
VPRIV bool cxx_kw_is_simple_type(int kw);
VPRIV bool cxx_kw_is_type_modifier(int kw);
VPRIV bool cxx_kw_is_cv(int kw);
VPRIV bool cxx_kw_is_decl_specifier(int kw);
VPRIV bool cxx_type_name_try(cxx *c, cxx_scope *ctor_class);
VPRIV void cxx_builtin_template(cxx *c, uint32_t id, const cxx_targ *a, uint32_t n);
VPRIV void cxx_spec_keyword(cxx_spec *sp, int kw);
VPRIV int cxx_decl_specifiers_parse(cxx *c, cxx_scope *ctor_class, bool is_template);
VPRIV bool cxx_ptr_operator_parse(cxx *c, qty *t);
VPRIV void cxx_template_arguments_then_paren(cxx *c);
VPRIV uint32_t cxx_operator_id_at(cxx *c, uint32_t n);
VPRIV void cxx_defaults_record(cxx *c, cxx_entity *e);
VPRIV uint32_t cxx_operator_name_parse(cxx *c);

/* declarator.c */
VPRIV bool cxx_paren_is_nested_declarator(cxx *c);
VPRIV int64_t cxx_literal_value(cxx *c);
VPRIV uint32_t cxx_declarator_parse(cxx *c, bool abstract_ok, bool *qualified, bool *is_function, cxx_scope **qscope);
VPRIV void cxx_parameter_clause_parse(cxx *c);
VPRIV void cxx_parameter_clause_body_parse(cxx *c);
VPRIV int cxx_noexcept_parse(cxx *c);
VPRIV void cxx_function_tail_parse(cxx *c);
VPRIV void cxx_type_specifiers_parse(cxx *c);
VPRIV void cxx_new_type_id_parse(cxx *c);
VPRIV uint32_t cxx_handler_declaration_parse(cxx *c);
VPRIV void cxx_type_id_parse(cxx *c);
VPRIV void cxx_type_id_then_end(cxx *c);
VPRIV bool cxx_try_type_id(cxx *c);

/* initializer.c */
VPRIV void cxx_braced_init_list_parse(cxx *c);
VPRIV void cxx_initializer_parse(cxx *c);
VPRIV void cxx_expression_list_parse(cxx *c);

/* body.c */
VPRIV void cxx_late_add(cxx *c, uint32_t begin, uint32_t end, int what);
VPRIV void cxx_late_pack_declare(cxx *c, const cxx_late *l, cxx_scope *fs);
VPRIV void cxx_params_declare(cxx *c, cxx_scope *fs, const cxx_param *ps, uint32_t n);
VPRIV void cxx_function_body_parse(cxx *c);
VPRIV void cxx_function_body_inner_parse(cxx *c);
VPRIV void cxx_function_body_defer(cxx *c);
VPRIV void cxx_late_parse_all(cxx *c, uint32_t from);

/* namespace.c */
VPRIV void cxx_namespace_parse(cxx *c);
VPRIV void cxx_using_parse(cxx *c);
VPRIV void cxx_static_assert_parse(cxx *c);
VPRIV uint32_t cxx_tparam_type_end(cxx *c, uint32_t p);

/* template.c */
VPRIV bool cxx_tokens_same(cxx *c, uint32_t a, uint32_t ae, uint32_t b, uint32_t be);
VPRIV void cxx_tparams_inherit_defaults(cxx *c, cxx_scope *ps, cxx_scope *in, uint32_t name);
VPRIV void cxx_template_parameter_parse(cxx *c);
VPRIV void cxx_requires_clause_parse(cxx *c);
VPRIV void cxx_template_declaration_parse(cxx *c, int context);

/* class.c */
VPRIV qty cxx_base_specifier_parse(cxx *c, cxx_scope *cs, bool *virtual_out);
VPRIV void cxx_base_clause_parse(cxx *c, cxx_scope *cs);
VPRIV void cxx_class_specifier_parse(cxx *c, bool is_template);
VPRIV uint32_t cxx_class_body_parse(cxx *c, cxx_scope *cs);
VPRIV void cxx_enum_specifier_parse(cxx *c);

/* init_declarator.c */
VPRIV bool cxx_is_function_body_start(cxx *c);
VPRIV bool cxx_declarator_is_assignment(cxx *c, uint32_t start);
VPRIV void cxx_init_declarators_parse(cxx *c, int flags, int context, cxx_scope *cs);

/* guide.c */
VPRIV int cxx_guide_tparam(cxx *c, uint32_t i);
VPRIV void cxx_guide_record(cxx *c, cxx_entity *te, uint32_t i);
VPRIV qty cxx_ctad(cxx *c, cxx_entity *te, const qty *args, const uint8_t *lval, uint32_t nargs);
VPRIV bool cxx_deduction_guide_try(cxx *c);

/* declaration.c */
VPRIV void cxx_simple_declaration_parse(cxx *c, int context, cxx_scope *cs);
VPRIV void cxx_member_declaration_parse(cxx *c, cxx_scope *cs);
VPRIV void cxx_member_declaration_parse1(cxx *c, cxx_scope *cs);
VPRIV bool cxx_starts_declaration(cxx *c);
VPRIV void cxx_declaration_parse(cxx *c, int context);
VPRIV void cxx_extern_member_record(cxx *c);
VPRIV bool cxx_member_is_extern(cxx *c, const cxx_entity *f);

/* signature.c */
VPRIV void cxx_class_specifier_for_instance(cxx *c);
VPRIV void cxx_translation_unit_parse(cxx *c);
VPRIV qty cxx_function_signature_parse(cxx *c, bool any_return, bool *body, cxx_scope *ctor_class);
VPRIV void cxx_late_parse_one(cxx *c, const cxx_late *l);

/* constraint.c */
VPRIV void cxx_for_range_declaration_parse(cxx *c);
VPRIV void cxx_lambda_parameters_parse(cxx *c);
VPRIV int cxx_requires_clause_value(cxx *c);
VPRIV cxx_entity *cxx_constraint_name_parse(cxx *c);
VPRIV void cxx_template_name_default_parse(cxx *c, cxx_targ *out);

#endif
