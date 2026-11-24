/*
 * lower-internal.h: lowering the semantic tree as C's AST (include/frontend/cplusplus/
 * lower.h; doc/PLAN-cxx-sema.md S1d), so that the code generator C uses
 * makes the translation unit's code.
 *
 * The semantic tree has every implicit step explicit, so each node maps to
 * the C node of the same operation; what C has no node for is said with
 * the ones it has:
 *
 *   - a reference is a pointer: a variable of reference type holds the
 *     address of the object it is bound to, a name of it is that address
 *     dereferenced, and binding it takes the address (a prvalue's
 *     temporary is a compound literal of the enclosing block);
 *   - a comparison, ! and the logical operators are int in C, so their
 *     value is converted to bool;
 *   - a condition that declares, and an init-statement, are blocks around
 *     the statement, as the standard's equivalences say;
 *   - a glvalue comma or conditional is the object whose address the C
 *     comma or conditional selects.
 *
 * The C AST takes the analysis's word for every rule: lowering checks
 * nothing.  A construct with no rule here fails the translation unit
 * (lowering_failure); nothing is guessed.
 */
#ifndef VCC_CPLUSPLUS_LOWER_INTERNAL_H
#define VCC_CPLUSPLUS_LOWER_INTERNAL_H

#include "../cplusplus-internal.h"
#include "frontend/cplusplus/builtin-registry.h"
#include "frontend/cplusplus/lower.h"

namespace cplusplus::lowering
{

/* C nodes said once */
inline uint32_t c_variable(cc *c, uint32_t s, ast_qualified_type t)
{
    return ast_builder_node(c, AST_NODE_VARIABLE, t, s, 0, 0, 0,
                            AST_NODE_FLAG_LVALUE);
}

inline uint32_t c_statement(cc *c, uint32_t e)
{
    return ast_builder_node(c, AST_NODE_EXPRESSION,
                            ast_builder_type_basic(AST_TYPE_VOID), e, 0, 0, 0,
                            0);
}

inline uint32_t c_declare(cc *c, uint32_t s, uint32_t init = 0)
{
    return ast_builder_node(c, AST_NODE_DECLARATION,
                            ast_builder_type_basic(AST_TYPE_VOID), s, init, 0,
                            0, 0);
}

/* an unnamed automatic variable of type t */
inline uint32_t c_local(cc *c, ast_qualified_type t)
{
    return ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t,
                              AST_STORAGE_AUTO, 0, 1);
}

class c_lowering
{
  public:
    c_lowering(const semantic_analysis &analysis, token_stream &tokens, cc *c,
               int target, const char *main_path, bool rtti)
        : analysis(analysis), tokens(tokens), c(c), target(target),
          main_path(main_path), rtti(rtti)
    {
        symbols.resize(analysis.entity_table.size());
        for (uint32_t &s : symbols)
            s = 0;
        records.resize(analysis.entity_table.size());
        for (ast_qualified_type &r : records)
            r = 0;
        variable_arrays.resize(analysis.type_table.size());
        for (ast_qualified_type &r : variable_arrays)
            r = 0;
        c_fields.resize(analysis.entity_table.size());
        for (uint32_t &f : c_fields)
            f = 0;
        deferred_class.resize(analysis.entity_table.size());
        for (uint8_t &f : deferred_class)
            f = 0;
        tail_reused.resize(analysis.entity_table.size());
        for (uint8_t &f : tail_reused)
            f = 0;
        used_symbols.resize(analysis.entity_table.size());
        for (uint8_t &f : used_symbols)
            f = 0;
        complete_symbols.resize(analysis.entity_table.size());
        for (uint32_t &s : complete_symbols)
            s = 0;
        deleting_symbols.resize(analysis.entity_table.size());
        for (uint32_t &s : deleting_symbols)
            s = 0;
        vtables.resize(analysis.entity_table.size());
        for (uint32_t &s : vtables)
            s = 0;
        vtts.resize(analysis.entity_table.size());
        for (uint32_t &s : vtts)
            s = 0;
    }

    bool translation_unit();

    lowering_failure failure;

  private:
    const semantic_analysis &analysis;
    token_stream &tokens;
    cc *c;
    int target;
    const char *main_path;
    /* -frtti: classes' type_info objects are made */
    bool rtti;
    /* [basic.start.dynamic]: the calls of the functions initializing the
       variables of namespace scope dynamically, in the order of their
       definitions ([basic.start.term]/1: or registering the destruction
       of a constant-initialized one) */
    owned_array<uint32_t> initializers;
    bool global_initializer(entity_index e, bool only_destroy);
    bool dynamic_initialization();
    /* [basic.start.term], [abi:guards]: the run time's functions and
       __dso_handle, 0 until declared */
    uint32_t cxa_atexit = 0, dso_handle = 0, guard_acquire = 0,
             guard_release = 0, noop_coro_symbol = 0;
    uint32_t runtime_call(uint32_t &slot, const char *name,
                          ast_qualified_type result,
                          const ast_qualified_type *parameters,
                          const uint32_t *arguments, uint32_t count);
    /* __cxa_atexit(d, object, &__dso_handle): d a function of type f */
    uint32_t destructor_registration(uint32_t d, ast_qualified_type f,
                                     uint32_t object);
    uint32_t initialization(entity_index e, uint32_t init, token_index at);
    void static_initialization(entity_index e, uint32_t init,
                               owned_array<uint32_t> &out, token_index at);
    void guarded_initialization(const semantic_node &n, entity_index destructor,
                                owned_array<uint32_t> &out);
    uint32_t local_function(const char *name, size_t length);
    /* the C symbol of each entity, 0 until made */
    owned_array<uint32_t> symbols;
    /* the C struct of each class, 0 until made */
    owned_array<ast_qualified_type> records;
    /* [ext:clang_vla] the C type of each variable length array type (by
       the type's index), made once: its bound is lowered once, where the
       array is first named (its declaration) */
    owned_array<ast_qualified_type> variable_arrays;
    /* a data member or base subobject: its field's number in its class's
       C struct, which has padding fields where C's layout would place a
       member earlier than the ABI does; a class: its C struct's number of
       fields */
    owned_array<uint32_t> c_fields;
    /* a base subobject whose field is the char array of its data size */
    owned_array<uint8_t> tail_reused;
    /* a class named through a pointer or reference while another class's
       struct is being made: its tag exists, its fields wait until the
       outermost class is complete (else a member of it that is the class
       being made would be an incomplete struct); the classes waiting,
       with where each was named */
    owned_array<uint8_t> deferred_class;
    owned_array<entity_index> deferred_classes;
    owned_array<token_index> deferred_at;
    /* the nesting depth of the classes being made */
    uint32_t classes_in_progress = 0;
    /* the labels of the function being lowered: identifier, C label */
    struct label_record {
        uint32_t identifier = 0, label = 0;
    };
    owned_array<label_record> labels;
    /* lowering a function's body (else a namespace-scope initializer) */
    bool in_function = false;
    /* the complete object constructor or destructor's symbol (C1, D1) of
       each special member function, 0 until made; symbols has the base
       object one (C2, D2) */
    owned_array<uint32_t> complete_symbols;
    /* each virtual destructor's deleting destructor (D0), 0 until made */
    owned_array<uint32_t> deleting_symbols;
    bool deleting_destructor(entity_index e, token_index at, bool define);
    /* [abi:vtable]: each dynamic class's virtual table symbol, 0 until
       made, and __cxa_pure_virtual's ([abi:pure-virtual]) */
    owned_array<uint32_t> vtables;
    uint32_t pure_virtual = 0;
    /* __cxa_deleted_virtual, the slot of a destructor defined as deleted */
    uint32_t deleted_virtual = 0;
    uint32_t vtable(entity_index k, token_index at);
    /* [abi:rtti]: type t's std::type_info object, the symbol _ZTI <type>
       (the exception lowering's way to a thrown or caught type's: call
       typeinfo_address for the void * a __cxa_throw or a handler's match
       takes).  Defined when the translation unit is its owner (a class's
       with its key function, else weak in every file using it) and declared
       when it is the run-time library's or another file's; 0 and a failure
       without -frtti (clang's -fno-rtti emits none) or for a type with no
       rules yet.  The table of a class's virtual table uses it too */
    uint32_t typeinfo(type_index t, token_index at);
    uint32_t typeinfo_address(type_index t, token_index at);
    bool typeinfo_internal(type_index t) const;
    bool class_internal(entity_index k) const;
    uint32_t typeinfo_class_vtable(const char *name);
    uint32_t string_symbol_pointer(uint32_t s, ast_qualified_type array,
                                   ast_qualified_type element);
    owned_array<type_index> typeinfo_types;
    owned_array<uint32_t> typeinfo_symbols;
    owned_array<ast_qualified_type> typeinfo_arrays;
    /* the library's virtual tables of the type_info classes, by name */
    owned_array<const char *> runtime_vtable_names;
    owned_array<uint32_t> runtime_vtables;
    ast_qualified_type vtable_type(entity_index k);
    /* the address point of class k's virtual table: &vtable[2]
       ([abi:vtable-general]: after the offset to top and the RTTI
       pointer), a void ** */
    uint32_t address_point(entity_index k, uint32_t group, token_index at);
    /* [abi:vtable-ctor]: a virtual table group as laid out for class k
       whose subobject is at base_offset in an object of class layout: k's
       own (k == layout) or a construction virtual table, which leaves out
       the tables of non-virtual bases without virtual bases
       ([abi:vtable-ctor-entries]) */
    struct table_spec {
        entity_index k = 0, layout = 0;
        uint64_t base_offset = 0;
        /* a construction virtual table; of a virtual base of layout */
        bool construction = false, is_virtual = false;
    };
    uint64_t virtual_base_offset(entity_index layout, entity_index v) const;
    /* the offset of group's subobject from k's in the table's layout */
    int64_t group_location(const table_spec &t, uint32_t group) const;
    bool group_included(const table_spec &t, uint32_t group) const;
    /* [abi:vtable-components]: the vbase and vcall offsets before group's
       offset to top, nearest the address point first */
    void group_offsets(const table_spec &t, uint32_t group,
                       owned_array<int64_t> &out) const;
    /* the number of pointers of the table, and the index of group's
       address point in it (UINT32_MAX if left out) */
    struct shape {
        uint32_t length = 0;
        owned_array<uint32_t> points, first_offset;
        owned_array<int64_t> offsets;
    };
    void table_shape(const table_spec &t, shape &out) const;
    uint32_t table_length(const table_spec &t) const;
    void define_array(uint32_t s, ast_qualified_type array,
                      const owned_array<uint32_t> &runs);
    /* the table's initializer, its symbol s defined with it */
    bool table_definition(const table_spec &t, uint32_t s, token_index at);
    /* &table[index], a void ** */
    uint32_t element_address(uint32_t s, ast_qualified_type array,
                             uint32_t index);
    /* [abi:vtable-ctor-vtt]: the VTT of class k: its virtual tables (the
       first k's own, the others construction virtual tables), its
       entries (a table's group's address point each), and where each
       base's sub-VTT starts */
    struct vtt_entry {
        uint32_t table = 0, group = 0;
    };
    struct sub_vtt {
        entity_index of = 0;
        uint64_t offset = 0;
        uint32_t index = 0;
    };
    struct vtt_layout {
        entity_index k = 0;
        owned_array<table_spec> tables;
        owned_array<vtt_entry> entries;
        owned_array<sub_vtt> subs;
    };
    void vtt_of(entity_index k, vtt_layout &out) const;
    void vtt_tables(vtt_layout &out, entity_index b, uint64_t at,
                    bool is_virtual) const;
    void vtt_pointer(vtt_layout &out, uint64_t at, uint32_t table) const;
    void vtt_secondary_pointers(vtt_layout &out, entity_index b, uint64_t at,
                                bool morally_virtual, uint32_t table,
                                owned_array<entity_index> &seen) const;
    /* whether e is not in seen yet, which it then is */
    static bool first_time(owned_array<entity_index> &seen, entity_index e)
    {
        for (entity_index s : seen)
            if (s == e)
                return false;
        seen.append(e);
        return true;
    }
    void vtt_virtual(vtt_layout &out, entity_index b,
                     owned_array<entity_index> &seen) const;
    /* class k's VTT symbol, defined with its construction virtual tables
       where k's virtual table is */
    owned_array<uint32_t> vtts;
    uint32_t vtt(entity_index k, token_index at);
    uint32_t vtt_define(entity_index k, uint32_t table, int storage,
                        uint32_t flags, token_index at);
    /* the index in class k's VTT of the sub-VTT of its base of class b at
       offset */
    uint32_t sub_vtt_index(entity_index k, entity_index b,
                           uint64_t offset) const;
    /* the address of the virtual base v of the object of class k at the
       address p (and again, a second copy of it): it plus
       the vbase offset in its virtual table ([abi:vtable-components]) */
    uint32_t virtual_base_address(uint32_t p, uint32_t again, entity_index k,
                                  entity_index v, ast_qualified_type t);
    uint32_t table_adjusted(uint32_t p, uint32_t again, int64_t index,
                            ast_qualified_type t);
    /* lowering a constructor's or destructor's body as one variant
       ([abi:mangling-special-ctor-dtor]): 1 complete object, 2 base object
       (which takes the VTT after this and leaves the virtual bases out),
       0 any other function; its class, its this and its VTT parameter */
    int structor_variant = 0;
    entity_index structor_class = 0, structor_this = 0;
    uint32_t vtt_parameter = 0;
    /* whether semantic node n is *this or this (in the function lowered) */
    bool at_this(uint32_t n) const;
    /* the base subobject (an entity) the object node n is of *this, 0 if
       none */
    entity_index base_subobject_of(uint32_t n) const;
    /* the VTT argument of a base object constructor or destructor call
       on base subobject b, 0 if its class has no virtual bases */
    uint32_t vtt_argument(entity_index b, token_index at);
    /* the C type of the base object constructor or destructor e of a
       class with virtual bases: (this, VTT, parameters) */
    ast_qualified_type structor_type(entity_index e, token_index at);
    uint32_t vtable_pointer_stores(const semantic_node &n);
    bool definition_body(entity_index e, uint32_t s, int variant);
    /* [abi:mangling-special-thunks]: the non-virtual thunk of virtual function f that adds
       adjustment to this, declared or (where f is defined) defined */
    /* and the virtual thunk ([abi:mangling-special-thunks] _ZTv): this
       plus adjustment, then plus the vcall offset at vcall bytes from
       the address point of the table it then points to; variant: a
       destructor's (1 complete object, 0 deleting) */
    struct thunk_record {
        entity_index f = 0;
        int64_t adjustment = 0, vcall = 0;
        int variant = 2;
        uint32_t symbol = 0;
        bool defined = false;
    };
    owned_array<thunk_record> thunks;
    uint32_t thunk(entity_index f, int64_t adjustment, token_index at,
                   int64_t vcall = 0, int variant = 2);
    bool thunk_definitions(entity_index f, token_index at);
    bool thunk_body(const thunk_record &r, token_index at);
    /* [abi:vtable-components]: the functions of virtual base v's vcall
       offsets, in order (v's own final overriders: the first of each
       signature in its tables not in its virtual bases) */
    struct vcall_entry {
        entity_index f = 0;
        uint32_t group = 0, slot = 0;
    };
    void vcall_functions(entity_index v, owned_array<vcall_entry> &out) const;
    /* slot i of group g, a virtual base's own table (offset 0 in it): the
       virtual base's own function there, and where the vcall offset a
       virtual thunk reads is (bytes from the address point, 0: the slot
       needs none, its function is the virtual base's own) */
    entity_index virtual_slot_own(const vtable_group &g, uint32_t i) const;
    int64_t vcall_offset_offset(const vtable_group &g, uint32_t i) const;
    /* the offset of the subobject of class f in the table's class k (the
       table's coordinates); false if not found once */
    bool subobject_location(const table_spec &t, entity_index f,
                            int64_t &out) const;
    /* set when a vcall offset's overrider was not located (the table is
       then refused) */
    mutable bool vcall_unknown = false;
    /* &((char *)p)[offset] as a pointer of type t */
    uint32_t offset_pointer(uint32_t p, int64_t offset, ast_qualified_type t);
    /* the pointer conversion n's operand plus offset bytes, a null pointer
       kept null */
    uint32_t null_checked_offset(const semantic_node &n, int64_t offset,
                                 ast_qualified_type t);
    uint32_t virtual_function(entity_index f, uint32_t object, token_index at,
                              uint32_t next = 0);
    bool side_effect_free(uint32_t lowered) const;
    /* [stmt.dcl]/2: the automatic objects with a destructor that are in
       scope where the lowering is, in the order they were constructed,
       with the statements a break or continue leaves; in a destructor,
       first the destruction of its class's members and bases
       ([class.dtor]/13), the semantic block in symbol */
    struct scope_record {
        enum kind_t : uint8_t {
            object,
            loop,
            switch_,
            subobjects
        } kind = object;
        uint32_t symbol = 0;
        /* an object's type: a class or an array of classes */
        qualified_type object_type;
        /* an object constructed in an operand that may not be evaluated:
           the bool variable set once it is ([class.temporary]/4), 0 if
           none */
        uint32_t flag = 0;
        /* the variable the object is (or whose reference it is bound to,
           [class.temporary]/6): what a goto's target has in scope */
        entity_index owner = 0;
        /* a loop whose continue is a goto to continue_label (a for
           statement whose condition declares: [stmt.for]/1 runs the step
           before the condition's variable is destroyed), leaving
           live[continue_mark ..] */
        uint32_t continue_label = 0;
        size_t continue_mark = 0;
        /* with exceptions: the void * variable holding the object's address, a cleanup variable
           whose function destroys the object when an exception unwinds past it ([except.ctor]/1);
           set to null where the lowering destroys the object itself; 0 if none */
        uint32_t guard = 0;
    };
    owned_array<scope_record> live;
    /* [except]: the exception run-time functions, 0 until declared; in a handler, the void *
       variable holding what __cxa_begin_catch returned; per destructor the function that
       destroys the object its guard points to */
    uint32_t cxa_allocate_exception = 0, cxa_throw = 0, cxa_rethrow = 0,
             cxa_begin_catch = 0;
    uint32_t exception_symbol = 0;
    owned_array<entity_index> unwind_destructors;
    owned_array<uint32_t> unwind_functions;
    uint32_t throw_expression(const semantic_node &n);
    uint32_t exception_object(const semantic_node &n);
    uint32_t try_statement(const semantic_node &n);
    uint32_t unwind_function(entity_index destructor, qualified_type object,
                             token_index at);
    uint32_t guard_object(uint32_t symbol, qualified_type object,
                          entity_index destructor, token_index at,
                          owned_array<uint32_t> &out);
    /* the declaration lowered now: the owner of what it makes live */
    entity_index declaring = 0;
    /* [stmt.dcl]/2 for a goto: per label of the function lowered, the
       automatic variables in scope at it, as identifier, count, then the
       variables; made at the function's first goto */
    owned_array<uint32_t> label_paths;
    bool label_paths_made = false;
    uint32_t function_root = 0;
    void label_scopes(uint32_t s, owned_array<uint32_t> &path);
    bool in_label_scope(uint32_t identifier, entity_index e) const;
    /* [class.temporary]/4: the flag of a temporary constructed where it
       may not be (in an operand of ?:, && or ||), 0 elsewhere; the
       construction made that sets it; a statement run only if it is set */
    uint32_t constructed_flag();
    uint32_t mark_constructed(uint32_t made, uint32_t flag);
    uint32_t if_constructed(uint32_t statement, uint32_t flag);
    /* [intro.execution]/5: the full-expression e as one C expression,
       its temporaries made and destroyed in it */
    uint32_t full_value(uint32_t e, bool discarded);
    /* an init-statement's statements, its variables made live */
    void init_statement(uint32_t n, owned_array<uint32_t> &out);
    /* for each element of the array in symbol s (type array, elements of
       type element), first to last or last to first: a call of special
       member e on its address (a construct's arguments, each element's
       own evaluation of them, [dcl.fct.default]/9) */
    uint32_t counted_loop(uint32_t i, ast_qualified_type t, uint32_t count,
                          bool reverse, uint32_t body);
    uint32_t each_element(uint32_t s, ast_qualified_type array,
                          ast_qualified_type element, uint64_t bound,
                          bool reverse, entity_index e, token_index at,
                          const semantic_node *construct = nullptr);

    /* declaring every function's symbol, which uses none */
    bool declaring_symbols = false;
    uint32_t fail(const char *what, token_index at)
    {
        if (!failure.what) {
            failure.what = what;
            failure.at = at;
        }
        return 0;
    }
    const struct type &record(qualified_type t) const
    {
        return analysis.type_table[t.unqualified()];
    }
    const semantic_node &node(uint32_t n) const
    {
        return analysis.semantics[n];
    }
    bool is_glvalue(const semantic_node &n) const
    {
        return n.category != value_category::prvalue;
    }

    /* types and symbols */
    ast_qualified_type type(qualified_type t, token_index at);
    ast_qualified_type fundamental(fundamental_type f, token_index at);
    ast_qualified_type class_type(entity_index e, token_index at);
    ast_qualified_type class_tag(entity_index e);
    ast_qualified_type pointee_type(qualified_type t, token_index at);
    ast_qualified_type entity_type(entity_index e, token_index at);
    bool is_reference(qualified_type t) const;
    bool without_linkage(qualified_type t) const;
    /* the function whose definition a block-scope entity is in, 0 if none */
    entity_index enclosing_function(entity_index e) const
    {
        return cplusplus::enclosing_function(analysis.entity_table, e);
    }
    /* the function a member of a local class (or of a class nested in
       one) is in, 0 for any other entity */
    entity_index local_class_function(entity_index e) const
    {
        return next_enclosing_function(analysis.entity_table, e);
    }
    uint32_t symbol(entity_index e, token_index at, int variant = 2);
    bool typeinfo_in_runtime(type_index t) const;
    void symbol_linkage(entity_index e, int &storage, uint32_t &flags) const;
    void gnu_attributes(entity_index e, uint32_t s);
    void spelling(entity_index e, owned_buffer &out);
    /* a constructor or destructor called: its symbol for a call */
    uint32_t special_callee(entity_index e, token_index at);
    /* a call of the constructor or destructor e on the object at the
       address object, with the arguments; with a VTT argument, of its
       base object variant */
    uint32_t special_call(entity_index e, uint32_t object,
                          const uint32_t *arguments, uint32_t count,
                          token_index at, uint32_t vtt_argument = 0);
    /* whether e is a constructor or destructor of a class with virtual
       bases ([abi:mangling-special-ctor-dtor]: its variants differ) */
    bool with_virtual_bases(entity_index e) const;
    /* the complete object variant of e, a constructor or destructor of a
       class with virtual bases; of an inline one, made once called */
    bool complete_variant(entity_index e);
    owned_array<entity_index> inline_structors;
    bool complete_variants();
    /* the destructor calls of live[mark ..], last constructed first */
    void destroy(size_t mark, owned_array<uint32_t> &out, token_index at);
    /* the live objects a break (switch_ too) or continue (not) leaves:
       live's index of the innermost loop or switch, 0 if none */
    size_t leaving(bool is_break) const;
    /* whether any object with a destructor is live */
    bool leaving_objects() const;
    uint32_t construct(const semantic_node &n);
    /* the constructor n.b of the construct node n called on the object
       lvalue object, with n's arguments */
    uint32_t construct_at(const semantic_node &n, uint32_t object, uint32_t vtt,
                          token_index at);
    /* the object in symbol s, of type object_type (C type t) */
    /* (through: symbol s is a pointer to the object) */
    uint32_t initialize_into(uint32_t s, qualified_type object_type,
                             ast_qualified_type t, uint32_t n,
                             bool through = false);
    bool piecewise_initializer(uint32_t n) const;
    uint32_t initialize_list_into(uint32_t object, qualified_type object_type,
                                  ast_qualified_type t, uint32_t n);
    uint32_t scalar_zero(qualified_type object, ast_qualified_type u,
                         token_index at);
    /* while it lives: the pointer new or delete holds (a variable of type
       t) and the element count array_count reads */
    struct allocation {
        c_lowering &l;
        uint32_t symbol, count;
        ast_qualified_type type;
        allocation(c_lowering &l, uint32_t s, ast_qualified_type t, uint32_t k)
            : l(l), symbol(l.allocated_symbol), count(l.count_symbol),
              type(l.allocated_type)
        {
            l.allocated_symbol = s;
            l.allocated_type = t;
            l.count_symbol = k;
        }
        ~allocation()
        {
            l.allocated_symbol = symbol;
            l.allocated_type = type;
            l.count_symbol = count;
        }
    };
    uint32_t temporary_object(uint32_t n);
    /* the class or array of classes type: its innermost element type in
       object, and that class's destructor (0 if none or not a class) */
    entity_index element_destructor(qualified_type type,
                                    qualified_type &object) const;
    /* the number of elements of type object in an array type (an array
       of arrays too), 0 if type is not an array */
    uint64_t element_count(qualified_type type, qualified_type object) const
    {
        if (type == object || !analysis.size_of_type(object))
            return 0;
        return analysis.size_of_type(type) / analysis.size_of_type(object);
    }
    /* [basic.start.term]/1: the destruction of the static object e (an
       array's elements, last first, by a function of its own) registered
       with __cxa_atexit, appended to out */
    void static_destruction(entity_index e, owned_array<uint32_t> &out,
                            token_index at);
    /* [impl.clang]: a function lowering makes, named as LLVM names a
       clash: base, then base.N with one counter for the module */
    owned_array<const char *> generated_names;
    unsigned generated_suffix = 0;
    uint32_t generated_function(const char *base);
    /* [dcl.stc]/8: the class or array of classes type has a mutable
       subobject (a const object of it is not in read-only memory) */
    bool has_mutable_subobject(qualified_type type) const;
    /* [expr.new], [expr.delete]: the pointer the enclosing new_object or
       delete_object holds (a variable of type allocated_type) */
    uint32_t allocated_symbol = 0;
    ast_qualified_type allocated_type = {};
    /* [abi:array-cookies]: the element count array_count reads */
    uint32_t count_symbol = 0;
    /* an array's elements, first to last or last to first: for (size_t i
       ...) { element = &first[i]; each } with allocated the element, as
       the node each of new_object or delete_object lowers */
    uint32_t element_loop(uint32_t first, ast_qualified_type object,
                          ast_qualified_type size, uint32_t count, bool reverse,
                          uint32_t each);
    uint32_t held_value(uint32_t value, ast_qualified_type t,
                        owned_array<uint32_t> &items);
    uint32_t new_object(const semantic_node &n);
    uint32_t new_array(const semantic_node &n, ast_qualified_type t);
    uint32_t delete_array(const semantic_node &n);
    uint32_t array_size(const semantic_node &n);
    uint32_t delete_object(const semantic_node &n);
    bool passed_indirectly(qualified_type t) const;
    /* a parameter's C type: a class passed by address is a pointer */
    ast_qualified_type parameter_type(qualified_type t, token_index at);
    bool indirect_parameter(entity_index e) const;
    /* [class.temporary]: the arguments' temporaries of the full-expression
       lowered now (collecting): their declarations, constructed where
       their argument is, and their destructor calls, run after it */
    bool collecting = false;
    /* [dcl.init]/17.6.1: the prvalue whose result object is the object
       being initialized (a declared variable, a returned value, an
       argument's temporary), and whether expression() is at it now */
    uint32_t result_object = 0;
    bool at_result_object = false;
    /* [class.copy.elision]/1.1: the automatic variable every return
       statement of the function lowered returns, constructed in the
       result object (clang's NRVO), 0 if none */
    entity_index named_return = 0;
    uint32_t initializer(uint32_t n);
    entity_index returned_variable(uint32_t return_operand) const;
    void find_named_return(uint32_t statement, entity_index &found,
                           bool &mixed) const;
    /* a temporary of type t of the full-expression being collected: its
       symbol, declared in temporaries */
    uint32_t full_expression_temporary(ast_qualified_type t);
    /* the temporary's destructor call, in destructions */
    /* [class.temporary]/6: the prvalue whose temporary lives as long as
       the reference it initializes (a declared variable's), not to the
       end of the full-expression */
    const semantic_node *extended_temporary = nullptr;
    /* [basic.stc.static]: the namespace-scope variable whose dynamic
       initialization is being lowered and the initializer's root node: a
       prvalue bound to the reference initialized there has static storage
       ([class.temporary]/6), _ZGR<name>_ */
    entity_index static_owner = 0;
    uint32_t static_root = 0;
    /* the temporaries of static_owner named so far (the discriminator of
       the next one's symbol, itanium_reference_temporary_name) */
    uint32_t static_temporaries = 0;
    /* the glvalue of that reference is being lowered: a temporary under it
       would be automatic, not extended */
    bool static_glvalue = false;
    uint32_t static_temporary(const semantic_node &n,
                              ast_qualified_type pointer);
    /* the destruction of the temporary of the prvalue n: at the end of
       the full-expression, or with the block's variables */
    void destroyed_temporary(const semantic_node &n, uint32_t temporary,
                             token_index at, uint32_t flag = 0);
    uint32_t destruction(uint32_t s, qualified_type object_type,
                         token_index at);
    unsigned conditional_depth = 0;
    owned_array<uint32_t> temporaries, destructions;
    uint32_t conditional(const semantic_node &n);
    uint32_t conditional_omitted(const semantic_node &n, ast_qualified_type t);
    uint32_t indirect_argument(uint32_t argument, qualified_type parameter,
                               token_index at);
    /* the full-expression e: its temporaries' declarations in before,
       their destruction in after */
    /* collecting the temporaries of a full-expression, from collect() to
       collected(): their declarations in before, their destruction in
       after */
    struct collection {
        bool outer = false;
        size_t first = 0, first_destruction = 0;
    };
    collection collect();
    void collected(const collection &k, owned_array<uint32_t> &before,
                   owned_array<uint32_t> &after);
    uint32_t full_expression(uint32_t e, bool discarded,
                             owned_array<uint32_t> &before,
                             owned_array<uint32_t> &after);

    /* expressions */
    uint32_t make(int op, ast_qualified_type t, uint32_t a, uint32_t b,
                  uint32_t c_, uint16_t aux, uint8_t flags)
    {
        return ast_builder_node(c, op, t, a, b, c_, aux, flags);
    }
    uint32_t cast(uint32_t operand, ast_qualified_type to, int kind)
    {
        return make(AST_NODE_CAST, to, operand, 0, 0,
                    static_cast<uint16_t>(kind), 0);
    }
    /* C types and nodes said once */
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type pointer_to(ast_qualified_type t)
    {
        return ast_builder_type_pointer(c, t);
    }
    uint32_t dereference(uint32_t p, ast_qualified_type t)
    {
        return make(AST_NODE_DEREFERENCE, t, p, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    }
    uint32_t assign(ast_qualified_type t, uint32_t a, uint32_t b)
    {
        return make(AST_NODE_ASSIGN, t, a, b, 0, 0, 0);
    }
    uint32_t comma(ast_qualified_type t, uint32_t a, uint32_t b)
    {
        return make(AST_NODE_COMMA, t, a, b, 0, 0, 0);
    }
    uint32_t statement_expression(ast_qualified_type t, uint32_t block_node)
    {
        return make(AST_NODE_STATEMENT_EXPRESSION, t, block_node, 0, 0, 0, 0);
    }
    uint32_t if_statement(uint32_t cond, uint32_t then, uint32_t otherwise)
    {
        return ast_builder_node(c, AST_NODE_IF, v, cond, then, otherwise, 0, 0);
    }
    uint32_t block(const owned_array<uint32_t> &items)
    {
        return block(items.data(), static_cast<uint32_t>(items.size()));
    }
    ast_qualified_type type_of(uint32_t n);
    /* C23's <stdbit.h> functions and __builtin_bitreverseg at run time */
    uint32_t stdc_bits(const semantic_node &n, ast_qualified_type t,
                       uint32_t argument);
    uint32_t special_builtin(builtin_lowering lowering, const semantic_node &n,
                             ast_qualified_type t,
                             const owned_array<uint32_t> &arguments);
    uint32_t to_bool(uint32_t int_value);
    uint32_t truth(uint32_t value);
    uint32_t to_ptrdiff(uint32_t integer);
    uint32_t expression(uint32_t n);
    uint32_t read(uint32_t n);
    uint32_t discard(uint32_t n);
    uint32_t object(uint32_t n);
    uint32_t member_access(const semantic_node &n, uint32_t object);
    uint32_t address_of(uint32_t lvalue, ast_qualified_type pointer);
    /* a read of a variable usable in constant expressions, lowered to its
       value */
    bool folded_read(const semantic_node &x) const;
    uint32_t data_size_assignment(const semantic_node &n, uint32_t a,
                                  uint32_t b);
    uint32_t constant(const semantic_node &n);
    uint32_t string_literal(const semantic_node &n);
    /* [ext:clang_source_location] __builtin_FILE() and the others, and the
       default argument whose are those of the call that uses it (the call,
       and the function it is in, while the argument is lowered) */
    uint32_t source_location(const semantic_node &n);
    uint32_t default_argument(const semantic_node &n);
    token_index call_site_at = 0;
    entity_index call_site_function = 0;
    uint32_t conversion(const semantic_node &n);
    /* pointers to members ([dcl.mptr], [abi:member-pointers]) */
    ast_qualified_type member_function_pair = 0;
    ast_qualified_type member_function_pointer_type();
    /* the C type of a member function of type function called on an
       object of type object: the object's address first ([class.this]) */
    ast_qualified_type member_function_type(qualified_type object,
                                            qualified_type function,
                                            token_index at);
    /* whether n is a member pointer constant (&C::m, a null member
       pointer) through conversions: the member (0: the null value) and
       the offset the conversions add */
    bool member_pointer_value(uint32_t n, entity_index &member,
                              int64_t &adjustment,
                              bool *variable = nullptr) const;
    uint32_t member_pointer(uint32_t n);
    uint32_t member_pointer_constant(qualified_type t, entity_index member,
                                     int64_t adjustment, token_index at);
    uint32_t null_member_pointer(qualified_type t, token_index at);
    uint32_t member_function_pointer_of(uint32_t ptr, uint32_t adj,
                                        token_index at);
    uint32_t member_pointer_conversion(const semantic_node &n);
    uint32_t member_pointer_object(const semantic_node &n);
    uint32_t member_pointer_test(uint32_t n, token_index at);
    uint32_t member_pointer_equality(const semantic_node &n, int op);
    uint32_t member_function_call(const semantic_node &n,
                                  owned_array<uint32_t> &items,
                                  uint32_t &object);
    /* a value read more than once: lowered again when that has no effect,
       else held in a temporary of the full-expression */
    struct reread {
        uint32_t node = 0, symbol = 0;
        ast_qualified_type type = 0;
    };
    uint32_t read_first(uint32_t n, reread &r, const char *what);
    uint32_t read_again(const reread &r);
    bool rereadable_member_pointer(uint32_t n) const;
    uint32_t binary(const semantic_node &n);
    uint32_t vector_lane(const semantic_node &n);
    uint32_t assignment(const semantic_node &n);
    uint32_t call(const semantic_node &n);
    uint32_t bind_reference(const semantic_node &n);
    uint32_t initializer_list(const semantic_node &n);
    uint32_t compound_literal(uint32_t n);
    uint32_t spaceship(const semantic_node &n);
    uint32_t compound_literal_of(uint32_t value, ast_qualified_type t);
    /* whether an object of type t has a pointer to data member in it,
       whose null value is not all zero bits */
    bool holds_data_member_pointer(qualified_type t) const;
    bool static_zero_initialization(const entity &x, uint32_t s,
                                    token_index at);
    void address_taken(uint32_t lvalue);

    /* statements */
    uint32_t statement(uint32_t n);
    uint32_t asm_statement(const semantic_node &n);
    void block_items(uint32_t first, uint32_t count,
                     owned_array<uint32_t> &out);
    void initialized_declaration(const semantic_node &n,
                                 owned_array<uint32_t> &out);
    void initialized_variable(const semantic_node &n,
                              owned_array<uint32_t> &out);
    void declaration_items(const semantic_node &n, owned_array<uint32_t> &out);
    uint32_t scoped_block(const semantic_node &n, uint32_t prefix);
    uint32_t loop_body(uint32_t n, bool is_switch);
    uint32_t loop(uint32_t cond_node, uint32_t step_node, uint32_t body_node,
                  token_index at);
    /* the statements of a condition that does not declare: none */
    owned_array<uint32_t> no_statements;
    uint32_t block(const uint32_t *items, uint32_t count);
    uint32_t declaration(const semantic_node &n);
    /* a condition's value; a condition that declares has its
       declaration's statements appended to out, its variable live */
    uint32_t condition(uint32_t n, owned_array<uint32_t> &out);
    uint32_t label(uint32_t identifier);
    bool function_definition(entity_index e);
    /* an inline function, a static one of a system header, or a member of
       an implicit instantiation ([temp.inst]/4: instantiated where used),
       that cannot be lowered is dropped, and a use of it refused: clang
       emits none that nothing uses (<immintrin.h> defines hundreds over
       builtins vcc lacks); one a use already named is not */
    bool function_definition_or_drop(entity_index e);
    /* per entity: a use made its symbol (symbol(), not declaring) */
    owned_array<uint8_t> used_symbols;
    struct dropped_function {
        entity_index entity;
        lowering_failure failure;
    };
    owned_array<dropped_function> dropped_functions;
    bool variable_definition(entity_index e);
    bool constant_initializer(uint32_t n) const;
    bool address_constant(uint32_t n) const;
    bool nonnull_address(uint32_t n) const;
    bool null_pointer(uint32_t n) const;
    bool needs_run_time(uint32_t n) const;
    bool static_lvalue(uint32_t n) const;
};

/* the C token of an operator, and whether it is a comparison
   (operators-and-calls.cpp) */
int c_operator(preprocessing_op_or_punc op);
bool is_comparison(int op);

} // namespace cplusplus::lowering

#endif
