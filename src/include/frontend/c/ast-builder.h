#ifndef VCC_FRONTEND_C_AST_BUILDER_H
#define VCC_FRONTEND_C_AST_BUILDER_H
/*
 * ast-builder.h: the AST that code generation reads (frontend/ast.h, the
 * records of frontend/c/cc.h), built by another front end: the C++ front
 * end's lowering (frontend/cplusplus/lower/, doc/PLAN-cxx-sema.md S1d).
 *
 * The builder is compiled in C's module and makes every record through
 * the C front end's own constructors, so the side tables codegen reads
 * (spans, interned types, pools) are as C's parser leaves them.  The AST's
 * conventions are C's:
 *
 *   - an lvalue node (AST_NODE_FLAG_LVALUE) used as a value is read by
 *     codegen: there is no load node; every other conversion is a CAST;
 *   - a function's body is an AST_NODE_BLOCK whose a/aux are the list and
 *     count of its parameter symbols (every parameter has one, name 0 if
 *     unnamed); an automatic variable is an AST_NODE_DECLARATION (a: sym,
 *     b: initializer), a static local only its symbol (body: initializer);
 *   - a label, case or default node's c is its statement;
 *   - AST_NODE_FOR's c is the list [step, body];
 *   - a pointer plus an integer has the pointer first and the integer
 *     converted to ptrdiff_t; comparisons and logical operators are int.
 *
 * Names are the symbol's: a symbol of file scope is named by its symbol
 * name (codegen adds Darwin's underscore), a static local by its assembler
 * name (ast_builder_symbol_assembler_name), which is written as given.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frontend/ast.h"

/* a translation unit's AST, its tables made as C's are for o's target; p
   receives codegen's diagnostics and gives -g's lines */
cc *ast_builder_open(pp *p, const cc_options *o);
/* the AST to read (the first member of cc) */
struct ast *ast_builder_ast(cc *c);
/* generate code for the symbols added with ast_builder_toplevel (as
   o->emit says); the number of errors */
int ast_builder_emit(cc *c);
void ast_builder_close(cc *c);

/* ---- types ---- */
ast_qualified_type ast_builder_type_basic(int kind); /* AST_TYPE_VOID .. */
ast_qualified_type ast_builder_type_qualified(cc *c, ast_qualified_type t,
                                              int qualifiers);
ast_qualified_type ast_builder_type_pointer(cc *c, ast_qualified_type to);
/* _Atomic(value), with C11's layout */
ast_qualified_type ast_builder_type_atomic(cc *c, ast_qualified_type value);
/* _Complex of a floating-point element */
ast_qualified_type ast_builder_type_complex(cc *c, ast_qualified_type element);
/* clang's ext_vector_type(count) of element (not bool) */
ast_qualified_type
ast_builder_type_ext_vector(cc *c, ast_qualified_type element, uint64_t count);
/* GNU vector_size of count elements of element (an arithmetic type, not bool) */
ast_qualified_type ast_builder_type_vector(cc *c, ast_qualified_type element,
                                           uint64_t count);
/* known false: an array of unknown bound */
ast_qualified_type ast_builder_type_array(cc *c, ast_qualified_type element,
                                          uint64_t count, bool known);
/* GNU C's variable length array of element: its bound is the expression
   size (a node of integer type, evaluated where the array's variable is
   declared and converted to size_t); the type is one per call, and sizes
   are captured once per declaration */
ast_qualified_type ast_builder_type_vla(cc *c, ast_qualified_type element,
                                        uint32_t size);
/* sizeof of the variable length array type t: a size_t read at run time */
uint32_t ast_builder_sizeof_vla(cc *c, ast_qualified_type t);
ast_qualified_type
ast_builder_type_function(cc *c, ast_qualified_type result,
                          const ast_qualified_type *parameters, uint32_t count,
                          bool variadic);
ast_qualified_type ast_builder_type_ptrdiff(cc *c);
/* the target's __builtin_va_list */
ast_qualified_type ast_builder_type_va_list(cc *c);
/* x86-64 System V: the element of the array that is __builtin_va_list */
ast_qualified_type ast_builder_type_va_list_tag(cc *c);
/* the target's size of t in bytes */
uint64_t ast_builder_type_size(cc *c, ast_qualified_type t);
/* a struct (a union if is_union) named name, incomplete until
   ast_builder_type_record_complete gives its fields (so that a field can
   point to it) */
ast_qualified_type ast_builder_type_record(cc *c, const char *name,
                                           size_t length, bool is_union);
/* the record's fields in order, laid out by C's rules; names[i] and
   lengths[i] name field i, aligned[i] is its aligned attribute's
   alignment in bytes (0: none), packed[i] whether it has the packed
   attribute, widths[i] its width if it is a bit-field (-1: it is not) */
/* a C++ class non-trivial for the purposes of calls: returned through the
   result address (x8 on AArch64, memory on x86-64) whatever its size */
void ast_builder_type_record_in_memory(cc *c, ast_qualified_type record);
void ast_builder_type_record_complete(
    cc *c, ast_qualified_type record, const ast_qualified_type *fields,
    const char *const *names, const size_t *lengths, const uint32_t *aligned,
    const uint8_t *packed, const int32_t *widths, uint32_t count);
/* the offset of field i of a complete record, in bytes */
uint64_t ast_builder_type_record_offset(cc *c, ast_qualified_type record,
                                        uint32_t field);
/* ... in bits (a bit-field's first bit) */
uint64_t ast_builder_type_record_bit_offset(cc *c, ast_qualified_type record,
                                            uint32_t field);
void ast_builder_type_record_set_offset(cc *c, ast_qualified_type record,
                                        uint32_t field, uint64_t bit_offset);
void ast_builder_type_record_set_size(cc *c, ast_qualified_type record,
                                      uint64_t size, uint32_t align);

/* ---- symbols ---- */
/* kind AST_SYMBOL_*, storage AST_STORAGE_*, flags AST_SYMBOL_FLAG_*; depth
   0 for file scope */
uint32_t ast_builder_symbol(cc *c, int kind, const char *name, size_t length,
                            ast_qualified_type t, int storage, uint32_t flags,
                            uint32_t depth);
void ast_builder_symbol_assembler_name(cc *c, uint32_t s, const char *name,
                                       size_t length);
/* emitted though nothing refers to it (attribute used); a static local
   is then a top-level symbol too */
void ast_builder_symbol_keep(cc *c, uint32_t s);
/* a function the program's start runs (.init_array; the default
   priority): GNU C's __attribute__((constructor)) */
void ast_builder_symbol_constructor(cc *c, uint32_t s);
/* ... or after main returns (.fini_array): __attribute__((destructor)) */
void ast_builder_symbol_destructor(cc *c, uint32_t s);
/* the priority of a constructor or destructor (0: the default; a smaller
   number runs first, a destructor's last) */
void ast_builder_symbol_priority(cc *c, uint32_t s, uint32_t priority);
/* GNU C's __attribute__((cleanup(function))) on a local variable: the
   function is called with the variable's address when its scope ends */
void ast_builder_symbol_cleanup(cc *c, uint32_t s, uint32_t function);
void ast_builder_symbol_flags_add(cc *c, uint32_t s, uint32_t flags);
/* With options->eh: the run-time symbols the landing pads of cleanup variables call (_Unwind_Resume;
   C++: __cxa_begin_catch and std::terminate, C: abort, for a cleanup that throws) and the personality
   routine (C++: __gxx_personality_v0, C: __gcc_personality_v0, as clang).  Once per TU, before a
   function with a cleanup variable is emitted; no effect without options->eh (doc/PLAN-vir-eh.md). */
void ast_builder_exceptions_ready(cc *c, bool cxx);
/* try/catch (doc/PLAN-vir-eh.md E3).  A handler's catch type is a symbol (the _ZTI object of the type,
   AST_SYMBOL_VARIABLE) or 0 for catch (...). */
typedef struct ast_builder_catch {
    uint32_t type; /* _ZTI symbol, 0: catch (...) */
    uint32_t body; /* the handler's statement */
} ast_builder_catch;
/* A try statement: body, then the handlers.  `exception` is a void * local the builder declares in the
   statement's own block (the caller does not): when a handler is entered it holds the pointer the
   unwinder delivered, the operand of __cxa_begin_catch.  The statement the handler runs to its end
   continues after the try; a call in `body` that unwinds is matched against the handlers in order
   (the selector against the type table, as [except.handle]), cleanup variables in between run, and none
   matching continues the unwinding.  The back end matches types only through the LSDA: the handler's
   body calls __cxa_begin_catch(exception) itself and keeps __cxa_end_catch on every exit by a
   cleanup variable whose function is ast_builder_end_catch_function (the call [except.handle]
   describes).  Returns the statement. */
uint32_t ast_builder_try(cc *c, uint32_t body, uint32_t exception,
                         const ast_builder_catch *handlers, uint32_t count);
/* void __cxa_end_catch(void *): the cleanup function of a handler's variable (it ignores the argument) */
uint32_t ast_builder_end_catch_function(cc *c);
/* GNU C's __attribute__((section(name))): the section the symbol is
   placed in */
void ast_builder_symbol_section(cc *c, uint32_t s, const char *name,
                                size_t length);
/* GNU C's __attribute__((visibility(name))): "hidden", "protected" */
void ast_builder_symbol_visibility(cc *c, uint32_t s, const char *name,
                                   size_t length);
/* GNU C's __attribute__((aligned(alignment))) on a variable: its least
   alignment in bytes */
void ast_builder_symbol_align(cc *c, uint32_t s, uint32_t alignment);
/* a function's body block, or a variable's initializer */
void ast_builder_symbol_body(cc *c, uint32_t s, uint32_t node);
/* the next definition or declaration of file scope, in order */
void ast_builder_toplevel(cc *c, uint32_t s);

/* ---- nodes ---- */
uint32_t ast_builder_node(cc *c, int op, ast_qualified_type t, uint32_t a,
                          uint32_t b, uint32_t c_, uint16_t aux, uint8_t flags);
uint32_t ast_builder_list(cc *c, const uint32_t *items, uint32_t count);
/* a builtin of the C front end's table named so (name[0..length)): its
   signature in the table's letters (the result, then the parameters), or
   NULL; ast_builder_builtin_call is its call of type t, the arguments
   converted to the parameters' types (0 if there is no such builtin) */
const char *ast_builder_builtin_signature(const char *name, size_t length);
uint32_t ast_builder_builtin_call(cc *c, const char *name, size_t length,
                                  ast_qualified_type t, const uint32_t *args,
                                  uint32_t count);
/* the C library function the builtin __builtin_NAME (name[0..length) is
   NAME) declares implicitly, as clang does: its prototype in the front
   end's builtin table letters (*clang_encoding false) or in clang's
   Builtins.def encoding (true), and the symbol it calls, NULL if that is
   NAME; false if there is no such builtin */
bool ast_builder_library_builtin(const char *name, size_t length,
                                 const char **signature, bool *clang_encoding,
                                 const char **symbol);
/* whether name is one of the __atomic_*, __sync_* and __c11_atomic_* calls:
   the number of arguments as written (the orders a __sync_ call lacks not
   counted), and the result: 0 void, 1 bool, 2 the type the first argument
   points to, 3 int, 4 unsigned int */
bool ast_builder_atomic_shape(const char *name, size_t length, int *written,
                              int *result);
/* __builtin_convertvector(vector, t): each element of vector converted
   to the element type of t, a vector type of as many elements */
/* __builtin_shufflevector(a, b, indices...): lanes of a then b, -1 any */
uint32_t ast_builder_shuffle_vector(cc *c, ast_qualified_type t, uint32_t a,
                                    uint32_t b, const int32_t *indices,
                                    uint32_t count);
/* __builtin_elementwise_min / _max of two vectors of one integer type */
/* ... _add_sat / _sub_sat */
uint32_t ast_builder_vector_saturate(cc *c, bool sub, uint32_t a, uint32_t b);
uint32_t ast_builder_vector_minmax(cc *c, bool max, uint32_t a, uint32_t b);
uint32_t ast_builder_vector_abs(cc *c, uint32_t a);
uint32_t ast_builder_vector_reduce(cc *c, int op, uint32_t a);
uint32_t ast_builder_complex(cc *c, ast_qualified_type t, uint32_t re,
                             uint32_t im);
uint32_t ast_builder_complex_part(cc *c, uint32_t v, int op);
uint32_t ast_builder_convert_vector(cc *c, ast_qualified_type t,
                                    uint32_t vector);
/* __atomic_load_n(pointer, order): a load of type t, pointer a node of
   type t * */
uint32_t ast_builder_atomic_load(cc *c, ast_qualified_type t, uint32_t pointer,
                                 uint32_t order);
/* field `field` of object, a node of record type: an lvalue if object is
   one */
uint32_t ast_builder_member(cc *c, uint32_t object, uint32_t field,
                            ast_qualified_type t);
/* value: the constant's 64 bits, sign-extended for a signed type and
   zero-extended for an unsigned one (unsigned __int128 included) */
uint32_t ast_builder_integer(cc *c, ast_qualified_type t, int64_t value);
uint32_t ast_builder_floating(cc *c, ast_qualified_type t, double value);
uint32_t ast_builder_floating_literal(cc *c, ast_qualified_type t,
                                      const char *spelling, size_t length);
/* a string literal of count elements of size bytes each, terminator
   included (bytes has them all); kind LEX_TOKEN_STRING .. */
uint32_t ast_builder_string(cc *c, ast_qualified_type element,
                            const void *bytes, uint32_t count, uint8_t size,
                            uint8_t kind);
/* a label (AST_NODE_LABEL's a, AST_NODE_GOTO's a); defined by the
   AST_NODE_LABEL node made with ast_builder_label_define */
uint32_t ast_builder_label(cc *c, const char *name, size_t length);
uint32_t ast_builder_label_define(cc *c, uint32_t label, uint32_t statement);
/* &&label (GNU): an AST_NODE_LABEL_ADDRESS of type void *; the label's
   address is taken, so a computed goto may reach it */
uint32_t ast_builder_label_address(cc *c, uint32_t label);
/* goto *address; (GNU): address a void * node */
uint32_t ast_builder_goto_indirect(cc *c, uint32_t address);
/* AST_NODE_CASE for value (or GNU case range lo ... hi), its statement s */
uint32_t ast_builder_case(cc *c, int64_t value, uint32_t s);
uint32_t ast_builder_case_range(cc *c, int64_t lo, int64_t hi, uint32_t s);

/* GNU asm: a string (the template, a constraint, a clobber) after its
   escape sequences, text[0..length) */
typedef struct ast_builder_asm_text {
    const char *text;
    size_t length;
} ast_builder_asm_text;
/* one operand: its [symbolic name] (text NULL: none), its constraint and
   the expression's node (an lvalue for an output) */
typedef struct ast_builder_asm_operand {
    ast_builder_asm_text name, constraint;
    uint32_t expr;
} ast_builder_asm_operand;
/* AST_NODE_ASM of an asm statement: operands[0 .. outputs) are the
   outputs, the rest inputs; extended is false for basic asm (no colon) */
uint32_t ast_builder_asm(cc *c, ast_builder_asm_text tmpl, bool is_volatile,
                         bool is_inline, bool extended,
                         const ast_builder_asm_operand *operands,
                         uint32_t outputs, uint32_t count,
                         const ast_builder_asm_text *clobbers,
                         uint32_t clobber_count);

#endif
