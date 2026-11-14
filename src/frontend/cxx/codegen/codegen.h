#ifndef CXX_CODEGEN_H
#define CXX_CODEGEN_H
/*
 * C++ code generation, first form (PLAN-cxx.md): while a function body is parsed the C++ parser builds
 * the shared AST for it with the C front end's builders (the same nodes, conversions and statements C
 * gives the emitter), and cxx_run hands the TU to emit_tu.  What is built so far: functions at namespace
 * scope over scalar types (integers, floating types, pointers): literals, variables, the built-in
 * operators, calls of such functions, if/while/do/return/break/continue, local variables.  A function
 * whose body has anything else is left undefined (c->gen_failed): declared, not emitted.
 */
#include "../cxx-internal.h"

/* a prvalue of class type built into a temporary (state.c: cxx_gen_prv_add) */
typedef struct cxx_prv {
	uint32_t node;		/* the prvalue as an expression: an lvalue naming the temporary */
	uint32_t expr;		/* the expression that constructs it (a call that takes the object's address first) */
	uint32_t call;		/* ... that call (0: built some other way, cannot be aimed elsewhere) */
	uint32_t tmp;		/* the temporary's symbol */
} cxx_prv;

/* VCC_CXX_GEN=4: the first thing not built in the function being built (state.c) */
#ifdef VCC_UNITY
static __thread char cxx_gen_why[256];
#else
extern __thread char cxx_gen_why[256];VPRIV uint32_t cxx_gen_check_missing(cxx *c);

#endif

/* the functions of codegen/, by file */

/* aggregate.c */
VPRIV bool cxx_gen_aggregate_init(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc);
VPRIV uint32_t cxx_gen_initializer_list(cxx *c, qty ilt, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc, uint32_t *array);

/* builtin.c */
VPRIV qty cxx_builtin_code_type(cxx *c, char code);
VPRIV const struct cxx_builtin_function *cxx_builtin_function_find(const char *name, size_t len, uint32_t nargs, size_t *index);
VPRIV int cxx_float_class_builtin(const char *name, size_t len);
VPRIV bool cxx_builtin_function_type(cxx *c, const char *name, size_t len, uint32_t nargs, qty arg0, qty *ret);
VPRIV void cxx_gen_builtin(cxx *c, const char *name, size_t len, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

/* call.c */
VPRIV void cxx_gen_function_value(cxx *c, cxx_entity *f, uint32_t loc);
VPRIV uint32_t cxx_gen_defaulted_eq(cxx *c, uint32_t x, uint32_t y, uint32_t loc);
VPRIV bool cxx_gen_std_cast_function(cxx *c, const cxx_entity *f);
VPRIV bool cxx_gen_std_namespace_function(cxx *c, const cxx_entity *f);
VPRIV uint32_t cxx_gen_array_cleanup(cxx *c, qty et, uint32_t n, uint32_t dsym, uint32_t loc);
VPRIV uint32_t cxx_gen_braced_temp(cxx *c, qty t, uint32_t pos, uint32_t loc);
VPRIV bool cxx_gen_defaulted_copy_trivial(cxx *c, qty t);
VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_gen_indirect_call(cxx *c, uint32_t callee, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_gen_operator_call(cxx *c, uint32_t obj, const uint32_t *args, uint32_t nargs, uint32_t loc);

/* construct.c */
VPRIV bool cxx_gen_needs_construction(cxx *c, qty t, int depth);
VPRIV void cxx_gen_destructor_parts_use(cxx *c, cxx_entity *d);
VPRIV void cxx_gen_defaulted_destructor(cxx *c, cxx_entity *d);
VPRIV cxx_entity *cxx_gen_declared_destructor(cxx *c, qty t);
VPRIV bool cxx_gen_needs_destruction(cxx *c, qty t, int depth);
VPRIV bool cxx_gen_copy_construct_parts(cxx *c, uint32_t target, qty t, uint32_t src, cc_id_list *out, uint32_t loc, int depth);
VPRIV bool cxx_gen_construct_at(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc);
VPRIV uint32_t cxx_gen_this_object(cxx *c, uint32_t loc);
VPRIV void cxx_gen_construct_local(cxx *c, uint32_t s, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV uint32_t cxx_gen_library_call(cxx *c, const char *name, const uint32_t *args, uint32_t nargs, uint32_t loc);

/* constructor.c */
VPRIV void cxx_gen_member_initializer(cxx *c, uint32_t name, qty class_type, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV bool cxx_gen_nsdmi(cxx *c, cxx_entity *fe, cc_id_list *first, uint32_t loc);
VPRIV bool cxx_gen_constructor_member_defaults(cxx *c, cxx_scope *cls, const tag *r, cc_id_list *first, uint32_t loc, int depth);
VPRIV bool cxx_gen_constructor_defaults(cxx *c, cc_id_list *first, uint32_t loc);
VPRIV cxx_entity *cxx_gen_constructor_body(cxx *c, qty t, cxx_entity *d);
VPRIV bool cxx_gen_construct_by_body(cxx *c, uint32_t target, qty t, cxx_entity *d, cc_id_list *out, uint32_t loc);

/* convert.c */
VPRIV void cxx_gen_construct_temp(cxx *c, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV uint32_t cxx_gen_class_argument(cxx *c, uint32_t x, qty pt, uint32_t loc);
VPRIV uint32_t cxx_gen_convert_class(cxx *c, uint32_t x, qty to, uint32_t loc);
VPRIV bool cxx_gen_is_nullptr_node(cxx *c, uint32_t x);
VPRIV uint32_t cxx_gen_nullptr_object(cxx *c, uint32_t x, uint32_t loc);
VPRIV uint32_t cxx_gen_bind(cxx *c, uint32_t x, qty to, uint32_t loc);
VPRIV uint32_t cxx_gen_referent(cxx *c, uint32_t x, uint32_t loc);
VPRIV uint32_t cxx_gen_base_pointer(cxx *c, uint32_t p, qty to, uint32_t loc);
VPRIV uint32_t cxx_gen_base_object(cxx *c, uint32_t x, qty to, uint32_t loc);
VPRIV uint32_t cxx_gen_member_of(cxx *c, uint32_t x, uint32_t name, uint32_t loc, int depth);

/* destructor.c */
VPRIV cxx_entity *cxx_gen_destructor_body(cxx *c, qty t, cxx_entity *d);
VPRIV cxx_entity *cxx_gen_effective_destructor(cxx *c, qty t, int depth);
VPRIV bool cxx_gen_destroy(cxx *c, uint32_t x, qty t, cc_id_list *out, uint32_t loc);
VPRIV bool cxx_gen_destructor_rest(cxx *c, cc_id_list *after, uint32_t loc);
VPRIV void cxx_gen_destroy_object(cxx *c, uint32_t x, bool arrow, uint32_t loc);

/* expression.c */
VPRIV void cxx_gen_integer(cxx *c, int64_t v, qty t, uint32_t loc);
VPRIV void cxx_gen_floating(cxx *c, const char *text, size_t len, qty t, uint32_t loc);
VPRIV void cxx_gen_variable(cxx *c, const cxx_entity *v, uint32_t loc);
VPRIV uint32_t cxx_gen_static_member_value(cxx *c, const cxx_entity *v, uint32_t loc);
VPRIV void cxx_gen_unary(cxx *c, int op, uint32_t x, uint32_t loc);
VPRIV void cxx_gen_postfix(cxx *c, int op, uint32_t x, uint32_t loc);
VPRIV void cxx_gen_string(cxx *c, const char *text, size_t len, uint32_t loc);
VPRIV void cxx_gen_cast(cxx *c, uint32_t x, qty to, uint32_t loc);
VPRIV uint32_t cxx_gen_empty_object(cxx *c, qty t, uint32_t loc);
VPRIV void cxx_gen_nullptr(cxx *c, uint32_t loc);
VPRIV void cxx_gen_reinterpret(cxx *c, uint32_t x, qty to, uint32_t loc);
VPRIV void cxx_gen_index(cxx *c, uint32_t base, uint32_t index, uint32_t loc);
VPRIV void cxx_gen_conditional(cxx *c, uint32_t cond, uint32_t x, uint32_t y, uint32_t loc);
VPRIV void cxx_gen_spaceship(cxx *c, uint32_t l, uint32_t r, qty t, uint32_t loc);
VPRIV void cxx_gen_comma(cxx *c, uint32_t x, uint32_t y, uint32_t loc);
VPRIV void cxx_gen_ordering_test(cxx *c, uint32_t ord, int op, uint32_t loc);
VPRIV void cxx_gen_binary(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc);
VPRIV void cxx_gen_assignment(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc);
VPRIV uint32_t cxx_gen_default_argument(cxx *c, const cxx_entity *f, uint32_t k);

/* function.c */
VPRIV bool cxx_gen_function_begin(cxx *c, cxx_entity *f, cxx_scope *fs, const cxx_param *ps, uint32_t nps);
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body);

/* global.c */
VPRIV const char *cxx_gen_symbol_asm_name(cxx *c, uint32_t s);
VPRIV void cxx_gen_file_asm(cxx *c, const char *text);
VPRIV uint32_t cxx_gen_try(cxx *c, uint32_t closure, cxx_entity *call, const uint32_t *ti, uint32_t nhandlers, uint32_t exc, uint32_t loc);
VPRIV uint32_t cxx_gen_scratch(cxx *c, qty t, uint32_t loc);
VPRIV uint32_t cxx_gen_return_zero(cxx *c, uint32_t body, uint32_t loc);
VPRIV uint32_t cxx_gen_try_statement(cxx *c, uint32_t call, const uint32_t *handlers, uint32_t nhandlers, uint32_t loc);
VPRIV bool cxx_gen_global_begin(cxx *c, const cxx_entity *v);
VPRIV bool cxx_gen_global_finish(cxx *c, bool keep);
VPRIV void cxx_gen_global_zeroed(cxx *c, const cxx_entity *v);
VPRIV uint32_t cxx_gen_unqualified(cxx *c, uint32_t x, uint32_t loc);
VPRIV void cxx_gen_global_object(cxx *c, const cxx_entity *v, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_gen_global_end(cxx *c, const cxx_entity *v, uint32_t init, bool has_init);

/* lambda.c */
VPRIV int cxx_gen_capture(cxx *c, cxx_gen_lambda *l, const cxx_entity *v, uint32_t loc);
VPRIV uint32_t cxx_gen_capture_name(uint32_t k);
VPRIV bool cxx_gen_lambda_symbol(cxx *c, cxx_entity *call, uint32_t serial);
VPRIV bool cxx_gen_lambda_finish(cxx *c, cxx_gen_lambda *l, cxx_entity *owner, uint32_t *body, uint32_t loc);
VPRIV void cxx_gen_lambda_object(cxx *c, cxx_gen_lambda *l, qty t, uint32_t loc);

/* name.c */
VPRIV void cxx_gen_member(cxx *c, uint32_t base, bool arrow, uint32_t name, uint32_t loc);
VPRIV uint32_t cxx_gen_dtor_sym(const cxx_entity *d);
VPRIV bool cxx_scope_in_unnamed_namespace(cxx *c, const cxx_scope *s);
VPRIV bool cxx_gen_has_this(const cxx_entity *f);
VPRIV qty cxx_gen_this_type(cxx *c, const cxx_entity *f);
VPRIV uint32_t cxx_gen_text(cxx *c, const char *text);
VPRIV bool cxx_gen_is_special(const cxx_entity *f);
VPRIV qty cxx_gen_abi_type(cxx *c, const cxx_entity *f);
VPRIV bool cxx_gen_name_defined(cxx *c, const char *nm);
VPRIV uint32_t cxx_gen_symbol_named(cxx *c, const char *name, uint32_t s);
VPRIV bool cxx_gen_variable_name(cxx *c, cxx_entity *v, bool internal);
VPRIV bool cxx_gen_name(cxx *c, cxx_entity *f);
VPRIV void cxx_gen_this(cxx *c, uint32_t loc);
VPRIV cxx_entity *cxx_member_function_find(cxx *c, cxx_scope *cls, uint32_t name, const cxx_param *ps, uint32_t nps, bool is_const);

/* new.c */
VPRIV void cxx_gen_new(cxx *c, qty t, const uint32_t *place, uint32_t nplace, uint32_t count, bool array, const uint32_t *init,
		       int ninit, uint32_t loc);
VPRIV void cxx_gen_delete(cxx *c, uint32_t p, bool array, uint32_t loc);

/* state.c */
VPRIV bool cxx_gen_on(cxx *c);
VPRIV void cxx_gen_push(cxx *c);
VPRIV void cxx_gen_pop(cxx *c);
VPRIV void cxx_gen_unwind(cxx *c, uint32_t depth);
VPRIV void cxx_gen_fail_at(cxx *c, const char *file, int line);
VPRIV bool cxx_gen_static_ok(cc *a, qty ty, uint32_t init);
VPRIV bool cxx_gen_type_ok(cxx *c, qty t);
VPRIV bool cxx_gen_class_ok2(cxx *c, qty t, bool objects);
VPRIV bool cxx_gen_object_type_ok(cxx *c, qty t);
VPRIV bool cxx_gen_is_reference(cxx *c, qty t);
VPRIV uint32_t cxx_gen_temporary(cxx *c, qty t, uint32_t loc);
VPRIV bool cxx_gen_has_base_bytes(cxx *c, qty t, int depth);
VPRIV int cxx_gen_trivial_abi(cxx *c, qty t, int depth);
VPRIV bool cxx_gen_by_address(cxx *c, qty t);
VPRIV bool cxx_gen_value_ok(cxx *c, qty t);
VPRIV cxx_prv *cxx_gen_prv_find(cxx *c, uint32_t node);
VPRIV void cxx_gen_prv_add(cxx *c, uint32_t node, uint32_t expr, uint32_t call, uint32_t tmp);
VPRIV uint32_t cxx_gen_prv_aim(cxx *c, cxx_prv *p, uint32_t address);
VPRIV uint32_t cxx_gen_class_temp(cxx *c, qty t, uint32_t loc);
VPRIV uint32_t cxx_gen_chain(cxx *c, const cc_id_list *l, uint32_t last, uint32_t loc);

/* statement.c */
VPRIV void cxx_gen_throw(cxx *c, uint32_t x, bool has_operand, uint32_t loc);
VPRIV void cxx_gen_declare_with(cxx *c, cc_id_list *list, uint32_t s, uint32_t init, uint32_t loc);
VPRIV uint32_t cxx_gen_catch_begin(cxx *c, uint32_t exc, qty decl_type, uint32_t *var, cc_id_list *list, uint32_t loc);
VPRIV void cxx_gen_statement(cxx *c, uint32_t node);
VPRIV uint32_t cxx_gen_new_statement(cxx *c, int op, uint32_t loc, uint32_t x, uint32_t y, uint32_t z);
VPRIV bool cxx_gen_range_for_begin(cxx *c, cxx_gen_range *r, uint32_t range, const uint32_t *elems, uint32_t nelems, qty elem, uint32_t loc);
VPRIV uint32_t cxx_gen_var_node_public(cxx *c, uint32_t s, uint32_t loc);
VPRIV uint32_t cxx_gen_range_for_element(cxx *c, cxx_gen_range *r);
VPRIV void cxx_gen_range_for_end(cxx *c, cxx_gen_range *r);
VPRIV uint32_t cxx_gen_condition(cxx *c, uint32_t e);
VPRIV uint32_t cxx_gen_switch_value(cxx *c, uint32_t e);
VPRIV uint32_t cxx_gen_case(cxx *c, int64_t lo, int64_t hi, uint32_t stmt, uint32_t loc);
VPRIV uint32_t cxx_gen_return(cxx *c, uint32_t e, bool has_value, uint32_t loc);
VPRIV uint32_t cxx_gen_local(cxx *c, uint32_t name, qty t, uint32_t loc);
VPRIV uint32_t cxx_gen_local_static(cxx *c, uint32_t name, qty t, uint32_t loc);
VPRIV bool cxx_gen_needs_construction_public(cxx *c, qty t);
VPRIV void cxx_gen_local_declare(cxx *c, uint32_t s, uint32_t init, bool has_init, uint32_t loc);
VPRIV uint32_t cxx_gen_array_initializer(cxx *c, qty t, uint32_t loc);

/* vtable.c */
VPRIV bool cxx_vt_is_dtor(const cxx_entity *e);
VPRIV bool cxx_vt_same(cxx *c, const cxx_entity *x, const cxx_entity *y);
VPRIV void cxx_vt_push(cxx_scope *cls, cxx_entity *e);
VPRIV void cxx_vtable_init(cxx *c, cxx_scope *cls);
VPRIV void cxx_vtable_declare(cxx *c, cxx_scope *cls, cxx_entity *e, bool virt_kw);
VPRIV bool cxx_vtable_usable(cxx *c, cxx_scope *cls);
VPRIV uint32_t cxx_vtable_symbol(cxx *c, cxx_scope *cls);
VPRIV uint32_t cxx_gen_vptr_store_at(cxx *c, cxx_scope *cls, uint32_t self, uint32_t loc);
VPRIV uint32_t cxx_gen_function_node(cxx *c, uint32_t s, uint32_t loc);
VPRIV uint32_t cxx_gen_vptr_store(cxx *c, cxx_scope *cls, uint32_t loc);
VPRIV bool cxx_gen_default_construct(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth);
VPRIV bool cxx_gen_default_construct_parts(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth);
VPRIV uint32_t cxx_gen_runtime_symbol(cxx *c, const char *name, bool pointer_param);
VPRIV void cxx_gen_deleting_destructor(cxx *c, cxx_entity *f);
VPRIV void cxx_gen_complete_object_aliases(cxx *c);
VPRIV void cxx_gen_vtables_emit(cxx *c);
VPRIV uint32_t cxx_gen_data_symbol(cxx *c, const char *name, uint32_t n);
VPRIV uint32_t cxx_gen_typeinfo(cxx *c, qty t);
VPRIV uint32_t cxx_gen_data_address(cxx *c, uint32_t s, uint32_t index);
VPRIV bool cxx_gen_class_data_here(cxx *c, cxx_scope *cls);
VPRIV void cxx_gen_typeinfos_emit(cxx *c);
VPRIV uint32_t cxx_gen_eh_symbol(cxx *c, int which);
VPRIV uint32_t cxx_gen_runtime_call(cxx *c, uint32_t fs, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV uint32_t cxx_gen_to_void_pointer(cxx *c, uint32_t n);

#endif
