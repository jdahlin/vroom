/*
 * lower.cpp: the semantic tree as C's AST (include/frontend/cplusplus/
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
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/lower.h"

namespace cplusplus
{

namespace
{

class c_lowering
{
  public:
    c_lowering(const semantic_analysis &analysis, token_stream &tokens, cc *c,
               int target, const char *main_path)
        : analysis(analysis), tokens(tokens), c(c), target(target),
          main_path(main_path)
    {
        symbols.resize(analysis.entity_table.size());
        for (uint32_t &s : symbols)
            s = 0;
        records.resize(analysis.entity_table.size());
        for (ast_qualified_type &r : records)
            r = 0;
        c_fields.resize(analysis.entity_table.size());
        for (uint32_t &f : c_fields)
            f = 0;
        tail_reused.resize(analysis.entity_table.size());
        for (uint8_t &f : tail_reused)
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
    /* [basic.start.dynamic]: the variables of namespace scope initialized
       dynamically, in the order of their definitions */
    owned_array<entity_index> dynamic;
    /* per entry of dynamic: 1 if it is constant-initialized and only its
       destructor is registered ([basic.start.term]/1) */
    owned_array<uint8_t> destroy_only;
    bool dynamic_initialization();
    /* [basic.start.term], [abi:guards]: the run time's functions and
       __dso_handle, 0 until declared */
    uint32_t cxa_atexit = 0, dso_handle = 0, guard_acquire = 0,
             guard_release = 0;
    uint32_t runtime_call(uint32_t &slot, const char *name,
                          ast_qualified_type result,
                          const ast_qualified_type *parameters,
                          const uint32_t *arguments, uint32_t count);
    uint32_t destructor_registration(entity_index destructor, uint32_t object,
                                     token_index at);
    uint32_t initialization(entity_index e, uint32_t init, token_index at);
    void guarded_initialization(const semantic_node &n, entity_index destructor,
                                owned_array<uint32_t> &out);
    uint32_t local_function(const char *name, size_t length);
    /* the C symbol of each entity, 0 until made */
    owned_array<uint32_t> symbols;
    /* the C struct of each class, 0 until made */
    owned_array<ast_qualified_type> records;
    /* a data member or base subobject: its field's number in its class's
       C struct, which has padding fields where C's layout would place a
       member earlier than the ABI does; a class: its C struct's number of
       fields */
    owned_array<uint32_t> c_fields;
    /* a base subobject whose field is the char array of its data size */
    owned_array<uint8_t> tail_reused;
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
    uint32_t vtable(entity_index k, token_index at);
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
    uint32_t table_length(const table_spec &t) const;
    uint32_t table_point(const table_spec &t, uint32_t group) const;
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
    void vtt_pointer(vtt_layout &out, entity_index b, uint64_t at,
                     uint32_t table) const;
    void vtt_secondary_pointers(vtt_layout &out, entity_index b, uint64_t at,
                                bool morally_virtual, uint32_t table,
                                owned_array<entity_index> &seen) const;
    void vtt_virtual(vtt_layout &out, entity_index b,
                     owned_array<entity_index> &seen) const;
    /* class k's VTT symbol, defined with its construction virtual tables
       where k's virtual table is */
    owned_array<uint32_t> vtts;
    uint32_t vtt(entity_index k, token_index at);
    uint32_t vtt_define(entity_index k, uint32_t table, int storage,
                        uint32_t flags, token_index at);
    /* the index in class k's VTT of the entry for its group (UINT32_MAX:
       none), and of the sub-VTT of its base of class b at offset */
    uint32_t vtt_index_of_group(entity_index k, uint32_t group) const;
    uint32_t sub_vtt_index(entity_index k, entity_index b,
                           uint64_t offset) const;
    /* the address of the virtual base v of the object of class k at the
       address p (and again, a second copy of it): it plus
       the vbase offset in its virtual table ([abi:vtable-components]) */
    uint32_t virtual_base_address(uint32_t p, uint32_t again, entity_index k,
                                  entity_index v, ast_qualified_type t);
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
    struct thunk_record {
        entity_index f = 0;
        int64_t adjustment = 0;
        uint32_t symbol = 0;
        bool defined = false;
    };
    owned_array<thunk_record> thunks;
    uint32_t thunk(entity_index f, int64_t adjustment, token_index at);
    bool thunk_definitions(entity_index f, token_index at);
    /* &((char *)p)[offset] as a pointer of type t */
    uint32_t offset_pointer(uint32_t p, int64_t offset, ast_qualified_type t);
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
        ast_qualified_type type = 0;
        entity_index destructor = 0;
        /* an array's: its element type and bound (0: not an array) */
        ast_qualified_type element = 0;
        uint64_t bound = 0;
    };
    owned_array<scope_record> live;
    /* for each element of the array in symbol s (type array, elements of
       type element), first to last or last to first: a call of special
       member e on its address */
    uint32_t each_element(uint32_t s, ast_qualified_type array,
                          ast_qualified_type element, uint64_t bound,
                          bool reverse, entity_index e, token_index at);

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
    ast_qualified_type entity_type(entity_index e, token_index at);
    bool is_reference(qualified_type t) const;
    bool without_linkage(qualified_type t) const;
    entity_index enclosing_function(entity_index e) const;
    entity_index local_class_function(entity_index e) const;
    uint32_t symbol(entity_index e, token_index at, int variant = 2);
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
    /* the class or array of classes type: its innermost element type in
       object, and that class's destructor (0 if none or not a class) */
    entity_index element_destructor(qualified_type type,
                                    qualified_type &object) const;
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
    uint32_t materialize(uint32_t value, qualified_type t, token_index at);
    /* a temporary of type t of the full-expression being collected: its
       symbol, declared in temporaries */
    uint32_t full_expression_temporary(ast_qualified_type t);
    /* the temporary's destructor call, in destructions */
    void destroyed_after(entity_index destructor, uint32_t temporary,
                         ast_qualified_type t, token_index at);
    unsigned conditional_depth = 0;
    owned_array<uint32_t> temporaries, destructions;
    uint32_t conditional(const semantic_node &n);
    uint32_t indirect_argument(uint32_t argument, qualified_type parameter,
                               token_index at);
    /* the full-expression e: its temporaries' declarations in before,
       their destruction in after */
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
    ast_qualified_type type_of(uint32_t n);
    uint32_t to_bool(uint32_t int_value);
    uint32_t truth(uint32_t value);
    uint32_t to_ptrdiff(uint32_t integer);
    uint32_t expression(uint32_t n);
    uint32_t read(uint32_t n);
    uint32_t discard(uint32_t n);
    uint32_t object(uint32_t n);
    uint32_t address_of(uint32_t lvalue, ast_qualified_type pointer);
    /* a read of a variable usable in constant expressions, lowered to its
       value */
    bool folded_read(const semantic_node &x) const;
    uint32_t data_size_assignment(const semantic_node &n, uint32_t a,
                                  uint32_t b);
    uint32_t constant(const semantic_node &n);
    uint32_t string_literal(const semantic_node &n);
    uint32_t conversion(const semantic_node &n);
    uint32_t binary(const semantic_node &n);
    uint32_t assignment(const semantic_node &n);
    uint32_t call(const semantic_node &n);
    uint32_t bind_reference(const semantic_node &n);
    uint32_t initializer_list(const semantic_node &n);
    uint32_t compound_literal(uint32_t n);
    void address_taken(uint32_t lvalue);

    /* statements */
    uint32_t statement(uint32_t n);
    void block_items(uint32_t first, uint32_t count,
                     owned_array<uint32_t> &out);
    void initialized_declaration(const semantic_node &n,
                                 owned_array<uint32_t> &out);
    void declaration_items(const semantic_node &n, owned_array<uint32_t> &out);
    uint32_t scoped_block(const semantic_node &n, uint32_t prefix);
    uint32_t loop_body(uint32_t n, bool is_switch);
    uint32_t block(const uint32_t *items, uint32_t count);
    uint32_t declaration(const semantic_node &n, bool in_block = false);
    uint32_t condition(uint32_t n, uint32_t &declaration_statement);
    uint32_t label(uint32_t identifier);
    bool function_definition(entity_index e);
    bool variable_definition(entity_index e);
    bool constant_initializer(uint32_t n) const;
    bool address_constant(uint32_t n) const;
    bool nonnull_address(uint32_t n) const;
    bool null_pointer(uint32_t n) const;
    bool needs_run_time(uint32_t n) const;
    bool static_lvalue(uint32_t n) const;
};

/* ---- types ---------------------------------------------------------- */

ast_qualified_type c_lowering::fundamental(fundamental_type f, token_index at)
{
    switch (f) {
    case fundamental_type::void_:
        return ast_builder_type_basic(AST_TYPE_VOID);
    case fundamental_type::bool_:
        return ast_builder_type_basic(AST_TYPE_BOOL);
    case fundamental_type::char_:
        return ast_builder_type_basic(AST_TYPE_CHAR);
    case fundamental_type::signed_char:
        return ast_builder_type_basic(AST_TYPE_SIGNED_CHAR);
    case fundamental_type::unsigned_char:
    case fundamental_type::char8_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_CHAR);
    case fundamental_type::short_int:
        return ast_builder_type_basic(AST_TYPE_SHORT);
    case fundamental_type::unsigned_short_int:
    case fundamental_type::char16_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_SHORT);
    case fundamental_type::int_:
        return ast_builder_type_basic(AST_TYPE_INT);
    case fundamental_type::unsigned_int:
    case fundamental_type::char32_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_INT);
    case fundamental_type::wchar_t_:
        /* [impl.clang]: the target's wchar_t, as C's (unsigned on Linux
           arm64) */
        if (target == PP_LINUX_AARCH64)
            return ast_builder_type_basic(AST_TYPE_UNSIGNED_INT);
        return ast_builder_type_basic(AST_TYPE_INT);
    case fundamental_type::long_int:
        return ast_builder_type_basic(AST_TYPE_LONG);
    case fundamental_type::unsigned_long_int:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG);
    case fundamental_type::long_long_int:
        return ast_builder_type_basic(AST_TYPE_LONG_LONG);
    case fundamental_type::unsigned_long_long_int:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
    case fundamental_type::int128:
        return ast_builder_type_basic(AST_TYPE_INT128);
    case fundamental_type::unsigned_int128:
        return ast_builder_type_basic(AST_TYPE_UINT128);
    case fundamental_type::float_:
        return ast_builder_type_basic(AST_TYPE_FLOAT);
    case fundamental_type::double_:
        return ast_builder_type_basic(AST_TYPE_DOUBLE);
    case fundamental_type::long_double:
        return ast_builder_type_basic(AST_TYPE_LONG_DOUBLE);
    case fundamental_type::float16:
        return ast_builder_type_basic(AST_TYPE_FLOAT16);
    case fundamental_type::std_nullptr_t:
        /* [basic.fundamental]/14: "A value of type std::nullptr_t is a
           null pointer constant"; it has a pointer's size */
        return ast_builder_type_pointer(c,
                                        ast_builder_type_basic(AST_TYPE_VOID));
    case fundamental_type::builtin_va_list:
        return ast_builder_type_va_list(c);
    default:
        fail("a bit-precise integer type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
}

bool c_lowering::is_reference(qualified_type t) const
{
    type_kind k = record(t).kind;
    return k == type_kind::lvalue_reference || k == type_kind::rvalue_reference;
}

/* whether t is made from a class with no linkage (an unnamed class no
   typedef names, [basic.link]/4) */
bool c_lowering::without_linkage(qualified_type t) const
{
    for (;;) {
        const struct type &r = record(t);
        switch (r.kind) {
        case type_kind::class_type: {
            const entity &x = analysis.entity_table[r.entity];
            return !x.name && !x.linkage_name;
        }
        case type_kind::pointer:
        case type_kind::lvalue_reference:
        case type_kind::rvalue_reference:
        case type_kind::array:
            t = r.operand;
            continue;
        case type_kind::function: {
            const qualified_type *p =
                analysis.type_table.parameters(t.unqualified());
            for (uint32_t i = 0; i < r.parameter_count; i++)
                if (without_linkage(p[i]))
                    return true;
            t = r.operand;
            continue;
        }
        default:
            return false;
        }
    }
}

/* the C type of a C++ type; a reference is a pointer to what it refers to */
ast_qualified_type c_lowering::type(qualified_type t, token_index at)
{
    const struct type &r = record(t);
    ast_qualified_type made = 0;
    switch (r.kind) {
    case type_kind::fundamental:
        made = fundamental(r.fundamental, at);
        break;
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        made = ast_builder_type_pointer(c, type(r.operand, at));
        break;
    case type_kind::array:
        made = ast_builder_type_array(c, type(r.operand, at), r.bound,
                                      r.bound_known);
        break;
    case type_kind::function: {
        owned_array<ast_qualified_type> parameters;
        const qualified_type *p =
            analysis.type_table.parameters(t.unqualified());
        for (uint32_t i = 0; i < r.parameter_count; i++)
            parameters.append(parameter_type(p[i], at));
        made =
            ast_builder_type_function(c, type(r.operand, at), parameters.data(),
                                      r.parameter_count, r.variadic);
        break;
    }
    case type_kind::class_type:
        made = class_type(r.entity, at);
        break;
    case type_kind::enumeration: {
        /* [dcl.enum]/8: an enumeration is its underlying type's values,
           and has its representation ([basic.fundamental]/10) */
        fundamental_type u = analysis.entity_table[r.entity].underlying;
        if (u == fundamental_type::none) {
            fail("an incomplete enumeration type", at);
            return ast_builder_type_basic(AST_TYPE_INT);
        }
        made = fundamental(u, at);
        break;
    }
    default:
        fail("a pointer to member type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
    /* the qualifiers are C's bits: const 1, volatile 2 */
    if (t.cv())
        made = ast_builder_type_qualified(c, made, static_cast<int>(t.cv()));
    return made;
}

/*
 * A class as a C struct of its non-static data members (and base class
 * subobjects that are not empty), in order, at the offsets of the Itanium
 * layout ([abi:class-types]), which are C's for a POD ([abi:layout]).
 * Where they are not, the struct says them anyway:
 *   - a member the ABI places after where C would (an empty subobject
 *     moved by a conflict, II) has a char array of padding before it;
 *   - a base whose tail padding the class reuses (a member placed before
 *     the base's sizeof ends: [abi:pod]) is an aligned char array of the
 *     base's data size, which a member access reads as the base's type;
 *   - the struct ends with padding to the class's size.
 * The offsets C gives are checked against the analysis's.
 */
ast_qualified_type c_lowering::class_type(entity_index e, token_index at)
{
    if (records[e])
        return records[e];
    const entity &x = analysis.entity_table[e];
    if (x.kind != entity_kind::class_type) {
        fail("a class whose definition was not analysed", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
    owned_buffer name;
    spelling(e, name);
    if (!x.name && x.linkage_name) {
        size_t length = 0;
        const char *s =
            tokens.pp.identifier_spelling(x.linkage_name - 1, &length);
        vbuffer_append(name.get(), s, length);
    }
    ast_qualified_type made =
        ast_builder_type_record(c, name.data(), name.size(), x.union_);
    records[e] = made;
    if (!x.size)
        /* incomplete: a pointer to it is all a program can have */
        return made;
    owned_array<ast_qualified_type> fields;
    owned_array<const char *> names;
    owned_array<size_t> lengths;
    owned_array<uint32_t> aligned;
    owned_array<uint8_t> packed;
    owned_array<int32_t> widths;
    owned_array<uint64_t> offsets;
    /* the generated names (__base, __pad), NUL-separated, for the fields
       whose names[] is null */
    owned_buffer generated;
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    uint64_t end = 0;
    auto add = [&](ast_qualified_type t, const char *n, size_t length,
                   uint32_t align, bool pack, int32_t width) {
        fields.append(t);
        names.append(n);
        lengths.append(length);
        aligned.append(align);
        packed.append(pack);
        widths.append(width);
    };
    const dynamic_class *dynamic = analysis.dynamic_of(e);
    if (dynamic && !dynamic->primary_base) {
        /* [abi:class-types] I: the virtual table pointer at offset 0 */
        ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
        add(ast_builder_type_pointer(c, ast_builder_type_pointer(c, v)),
            "__vptr", 6, 0, false, -1);
        offsets.append(0);
        end = analysis.pointer_size();
    }
    auto padding = [&](uint64_t bytes) {
        vbuffer_format_append(generated.get(), "__pad%u",
                              static_cast<uint32_t>(fields.size()));
        vbuffer_append(generated.get(), "", 1);
        add(ast_builder_type_array(c, byte, bytes, true), nullptr, 0, 0, false,
            -1);
        offsets.append(UINT64_MAX);
    };
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index member = analysis.class_members[x.first_member + i];
        const entity &m = analysis.entity_table[member];
        ast_qualified_type t = type(m.type, at);
        uint64_t size = analysis.size_of_type(m.type);
        uint64_t align = analysis.alignment_of_type(m.type);
        if (is_reference(m.type)) {
            /* a pointer in the C struct */
            size = analysis.pointer_size();
            align = size;
        }
        if (m.gnu_flags & gnu_packed)
            align = 1;
        if (m.alignment > align)
            align = m.alignment;
        if (!x.union_ && !m.bit_field) {
            uint64_t natural = end;
            if (align)
                natural = (end + align - 1) / align * align;
            if (m.offset > natural && m.offset > end)
                padding(m.offset - end);
            /* the next member placed inside this base's tail padding */
            uint64_t next = x.size;
            if (i + 1 < x.member_count)
                next = analysis
                           .entity_table[analysis.class_members[x.first_member +
                                                                i + 1]]
                           .offset;
            const entity &of = analysis.entity_table[record(m.type).entity];
            if (m.base_subobject && of.virtual_base_count) {
                /* a base with virtual bases: its non-virtual part
                   ([abi:class-types] nvsize), its virtual bases the
                   class's own */
                size = of.nv_size;
                align = of.nv_alignment;
                t = ast_builder_type_array(c, byte, size, true);
                tail_reused[member] = 1;
            } else if (m.base_subobject && next < m.offset + size) {
                size = of.data_size;
                t = ast_builder_type_array(c, byte, size, true);
                tail_reused[member] = 1;
            }
        }
        c_fields[member] = static_cast<uint32_t>(fields.size());
        offsets.append(m.offset);
        int32_t width = -1;
        if (m.bit_field)
            width = m.bit_width;
        uint32_t field_align = m.alignment;
        if (tail_reused[member])
            /* the data-size char array: its base's alignment */
            field_align = static_cast<uint32_t>(align);
        if (m.base_subobject || (!m.name && !m.bit_field)) {
            /* a base class subobject or an anonymous union's object: a
               field no C name can refer to */
            vbuffer_format_append(generated.get(), "__base%u", i);
            vbuffer_append(generated.get(), "", 1);
            add(t, nullptr, 0, field_align, (m.gnu_flags & gnu_packed) != 0,
                width);
        } else if (!m.name) {
            /* an unnamed bit-field */
            add(t, "", 0, field_align, (m.gnu_flags & gnu_packed) != 0, width);
        } else {
            size_t length = 0;
            const char *spelled =
                tokens.pp.identifier_spelling(m.name - 1, &length);
            add(t, spelled, length, field_align,
                (m.gnu_flags & gnu_packed) != 0, width);
        }
        if (m.bit_field)
            end = (m.bit_offset + m.bit_width + 7) / 8;
        else if (m.offset + size > end)
            end = m.offset + size;
    }
    /* [abi:class-types] III: the virtual bases of a complete object, each
       its non-virtual part (an empty one has no bytes); no C name refers
       to them, the lowering reaches them through their offsets.  They
       are placed in inheritance graph order, not in the order of the
       list (clang's vbases()): by offset here */
    owned_array<uint32_t> placed;
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        uint32_t at_index = static_cast<uint32_t>(placed.size());
        uint64_t offset =
            analysis
                .entity_table
                    [analysis.class_virtual_bases[x.first_virtual_base + i]]
                .offset;
        while (at_index &&
               analysis.entity_table
                       [analysis.class_virtual_bases[x.first_virtual_base +
                                                     placed[at_index - 1]]]
                           .offset > offset)
            at_index--;
        placed.append(0);
        for (uint32_t j = static_cast<uint32_t>(placed.size()) - 1;
             j > at_index; j--)
            placed[j] = placed[j - 1];
        placed[at_index] = i;
    }
    for (uint32_t i : placed) {
        const entity &v =
            analysis.entity_table
                [analysis.class_virtual_bases[x.first_virtual_base + i]];
        const entity &of = analysis.entity_table[record(v.type).entity];
        if (!of.member_count && !of.dynamic)
            continue;
        if (v.offset > end)
            padding(v.offset - end);
        vbuffer_format_append(generated.get(), "__vbase%u", i);
        vbuffer_append(generated.get(), "", 1);
        add(ast_builder_type_array(c, byte, of.nv_size, true), nullptr, 0,
            of.nv_alignment, false, -1);
        offsets.append(v.offset);
        end = v.offset + of.nv_size;
    }
    if (!x.member_count && !end) {
        /* [intro.object]/8: an empty class is one byte, a C struct without
           members none: a char of padding */
        add(byte, "__empty", 7, 0, false, -1);
        offsets.append(UINT64_MAX);
        end = 1;
    }
    if (!x.union_ && end < x.size) {
        /* the C struct's size, rounded to its alignment, may fall short of
           the class's: padding at the end where it would */
        uint64_t rounded = end;
        if (x.alignment)
            rounded = (end + x.alignment - 1) / x.alignment * x.alignment;
        if (rounded < x.size)
            padding(x.size - end);
    }
    for (size_t i = 0, at_name = 0; i < names.size(); i++) {
        if (names[i])
            continue;
        names[i] = generated.data() + at_name;
        lengths[i] = strlen(names[i]);
        at_name += lengths[i] + 1;
    }
    c_fields[e] = static_cast<uint32_t>(fields.size());
    ast_builder_type_record_complete(
        c, made, fields.data(), names.data(), lengths.data(), aligned.data(),
        packed.data(), widths.data(), static_cast<uint32_t>(fields.size()));
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index member = analysis.class_members[x.first_member + i];
        const entity &m = analysis.entity_table[member];
        uint32_t f = c_fields[member];
        if (ast_builder_type_record_offset(c, made, f) != m.offset ||
            (m.bit_field &&
             ast_builder_type_record_bit_offset(c, made, f) != m.bit_offset))
            fail("a class whose layout is not C's", at);
    }
    if (ast_builder_type_size(c, made) != x.size)
        fail("a class whose size is not C's", at);
    if (!analysis.trivial_class(e))
        /* [abi:calls]: "If the return type is a class type that is
           non-trivial for the purposes of calls, the caller passes an
           address as an implicit parameter" (x8 on AArch64), whatever its
           size */
        ast_builder_type_record_in_memory(c, made);
    return made;
}

/* the C type of a function or variable: a non-static member function has
   the object's address first ([class.this]) */
/* [abi:calls]: a class that is not trivially copyable or has a
   non-trivial destructor is passed and returned by address, which C's
   calling convention does not do */
bool c_lowering::passed_indirectly(qualified_type t) const
{
    const struct type &r = record(t);
    return r.kind == type_kind::class_type &&
           analysis.entity_table[r.entity].kind == entity_kind::class_type &&
           !analysis.trivial_class(r.entity);
}

/* [abi:calls]: "If the parameter type is non-trivial for the purposes of
   calls, the caller must allocate space for a temporary and pass that
   temporary by reference" */
ast_qualified_type c_lowering::parameter_type(qualified_type t, token_index at)
{
    if (passed_indirectly(t))
        return ast_builder_type_pointer(c, type(t, at));
    return type(t, at);
}

bool c_lowering::indirect_parameter(entity_index e) const
{
    const entity &x = analysis.entity_table[e];
    return x.kind == entity_kind::variable && x.enclosing &&
           analysis.entity_table[x.enclosing].kind ==
               entity_kind::block_scope &&
           analysis.entity_table[x.enclosing].parameter_scope &&
           passed_indirectly(x.type);
}

ast_qualified_type c_lowering::entity_type(entity_index e, token_index at)
{
    const entity &x = analysis.entity_table[e];
    if (indirect_parameter(e))
        return parameter_type(x.type, at);
    if (x.kind != entity_kind::function || !x.nonstatic_member)
        return type(x.type, at);
    const struct type &f = record(x.type);
    owned_array<ast_qualified_type> parameters;
    qualified_type object = qualified_type::make(
        analysis.entity_table[x.enclosing].type.unqualified(), x.member_cv);
    parameters.append(ast_builder_type_pointer(c, type(object, at)));
    const qualified_type *p =
        analysis.type_table.parameters(x.type.unqualified());
    for (uint32_t i = 0; i < f.parameter_count; i++)
        parameters.append(parameter_type(p[i], at));
    return ast_builder_type_function(c, type(f.operand, at), parameters.data(),
                                     f.parameter_count + 1, f.variadic);
}

/* ---- symbols -------------------------------------------------------- */

/* the function whose definition a block-scope entity is in, 0 if none */
entity_index c_lowering::enclosing_function(entity_index e) const
{
    const entities &table = analysis.entity_table;
    entity_index s = table[e].enclosing;
    while (s && table[s].kind == entity_kind::block_scope)
        s = table[s].enclosing;
    if (s && table[s].kind == entity_kind::function)
        return s;
    return 0;
}

/* the function a member of a local class (or of a class nested in one)
   is in, 0 for any other entity */
entity_index c_lowering::local_class_function(entity_index e) const
{
    const entities &table = analysis.entity_table;
    entity_index c = table[e].enclosing;
    if (!c || table[c].kind != entity_kind::class_type)
        return 0;
    while (table[table[c].enclosing].kind == entity_kind::class_type)
        c = table[c].enclosing;
    return enclosing_function(c);
}

void c_lowering::spelling(entity_index e, owned_buffer &out)
{
    uint32_t id = analysis.entity_table[e].name;
    if (!id)
        return;
    size_t length = 0;
    /* the identifier's number is the preprocessor's plus one */
    const char *s = tokens.pp.identifier_spelling(id - 1, &length);
    vbuffer_append(out.get(), s, length);
}

/* the C symbol of a function or variable, made at its first use: named by
   its symbol (itanium_symbol_name) if it has one, by its identifier if it
   is automatic */
uint32_t c_lowering::symbol(entity_index e, token_index at, int variant)
{
    const entity &x = analysis.entity_table[e];
    uint32_t *slot = &symbols[e];
    if (x.special != special_member::none && variant == 1)
        slot = &complete_symbols[e];
    if (x.special == special_member::destructor && variant == 0)
        slot = &deleting_symbols[e];
    if (*slot)
        return *slot;
    if (x.kind != entity_kind::function && x.kind != entity_kind::variable)
        return fail("an entity that is not a function or variable", at);
    if (x.kind == entity_kind::variable && x.nonstatic_member)
        return fail("a non-static data member as an object", at);
    ast_qualified_type t = entity_type(e, at);
    if (variant == 2 && with_virtual_bases(e))
        t = structor_type(e, at);
    owned_buffer name;
    uint32_t s = 0;
    if (x.kind == entity_kind::variable && x.automatic) {
        spelling(e, name);
        s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(), name.size(),
                               t, AST_STORAGE_AUTO, 0, 1);
        *slot = s;
        gnu_attributes(e, s);
        return s;
    }
    if (!itanium_symbol_name(analysis, tokens.pp, e, name.get(), variant))
        return fail("an entity whose symbol is not mangled yet", at);
    int kind = AST_SYMBOL_VARIABLE;
    if (x.kind == entity_kind::function)
        kind = AST_SYMBOL_FUNCTION;
    if (enclosing_function(e)) {
        /* a local static: [basic.stc.static]/1, its symbol the mangled one
           (codegen writes an assembler name as given) */
        owned_buffer written;
        if (target == PP_DARWIN)
            vbuffer_string_append(written.get(), "_");
        vbuffer_append(written.get(), name.data(), name.size());
        owned_buffer identifier;
        spelling(e, identifier);
        s = ast_builder_symbol(c, kind, identifier.data(), identifier.size(), t,
                               AST_STORAGE_STATIC, 0, 1);
        ast_builder_symbol_assembler_name(c, s, written.data(), written.size());
        *slot = s;
        gnu_attributes(e, s);
        return s;
    }
    int storage = AST_STORAGE_NONE;
    if (x.linkage_of_name == linkage::internal)
        storage = AST_STORAGE_STATIC;
    /* [impl.clang]: an entity whose type has no linkage (an unnamed
       class's) cannot be named in another translation unit, so its symbol
       is local, its name unmangled as an external one's */
    else if (x.defined && without_linkage(x.type))
        storage = AST_STORAGE_STATIC;
    else if (!x.defined && kind == AST_SYMBOL_VARIABLE)
        storage = AST_STORAGE_EXTERN;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    /* [basic.def.odr]/11: an inline function or variable is defined in
       every translation unit that odr-uses it: emitted where referenced,
       weak so that the copies link as one ([abi:vague-inline] wants a
       COMDAT group too, which C's code generator does not make yet) */
    instantiation_kind explicit_ = analysis.explicitly_instantiated(e);
    if (explicit_ == instantiation_kind::definition)
        /* [temp.explicit]/10's explicit instantiation definition: emitted
           whether used or not, weak (clang's weak_odr) */
        flags |= AST_SYMBOL_FLAG_WEAK;
    else if (x.inline_ && explicit_ != instantiation_kind::declaration)
        flags |= AST_SYMBOL_FLAG_LINKONCE | AST_SYMBOL_FLAG_WEAK;
    /* [class.local]: a member of a local class has no linkage
       ([basic.link]/8); [impl.clang] its symbol is local unless the
       function the class is in is inline (then its copies link as one) */
    entity_index local = local_class_function(e);
    if (local && !analysis.entity_table[local].inline_) {
        storage = AST_STORAGE_STATIC;
        flags = AST_SYMBOL_FLAG_GLOBAL;
    }
    s = ast_builder_symbol(c, kind, name.data(), name.size(), t, storage, flags,
                           0);
    *slot = s;
    ast_builder_toplevel(c, s);
    gnu_attributes(e, s);
    return s;
}

/* [ext:gnu_attributes] what the entity's GNU attributes ask of its
   symbol, as C's front end gives them */
void c_lowering::gnu_attributes(entity_index e, uint32_t s)
{
    const entity &x = analysis.entity_table[e];
    uint32_t flags = 0;
    if (x.gnu_flags & gnu_noinline)
        flags |= AST_SYMBOL_FLAG_NOINLINE;
    if (x.gnu_flags & gnu_always_inline)
        flags |= AST_SYMBOL_FLAG_ALWAYS_INLINE;
    if (x.gnu_flags & gnu_noreturn)
        flags |= AST_SYMBOL_FLAG_NORETURN;
    if (x.gnu_flags & gnu_weak)
        flags |= AST_SYMBOL_FLAG_WEAK;
    if (flags)
        ast_builder_symbol_flags_add(c, s, flags);
    if (x.gnu_flags & gnu_used)
        ast_builder_symbol_keep(c, s);
    if (x.section) {
        /* the string-literal's characters between its quotes (sema took
           one with no escape sequence) */
        owned_buffer written;
        tokens.spelling(x.section, written.get());
        ast_builder_symbol_section(c, s, written.data() + 1,
                                   written.size() - 2);
    }
    if (x.kind == entity_kind::variable && x.alignment)
        ast_builder_symbol_align(c, s, x.alignment);
}

/* [abi:mangling-special-ctor-dtor]: an inline constructor or destructor
   is emitted as its base object variant alone, which its calls use (clang
   makes the complete object one an alias of it); another's calls use the
   complete object variant */
uint32_t c_lowering::special_callee(entity_index e, token_index at)
{
    int variant = 1;
    if (analysis.entity_table[e].inline_ && !with_virtual_bases(e))
        variant = 2;
    uint32_t s = symbol(e, at, variant);
    if (s)
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
    return s;
}

uint32_t c_lowering::special_call(entity_index e, uint32_t object,
                                  const uint32_t *arguments, uint32_t count,
                                  token_index at, uint32_t vtt_argument)
{
    uint32_t s = 0;
    ast_qualified_type f = 0;
    if (vtt_argument) {
        /* the base object variant, given its sub-VTT */
        s = symbol(e, at, 2);
        if (s)
            ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
        f = structor_type(e, at);
    } else {
        s = special_callee(e, at);
        f = entity_type(e, at);
    }
    if (!s || !object)
        return 0;
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, f, s, 0, 0, 0, 0),
             ast_builder_type_pointer(c, f), AST_CAST_FUNCTION_DECAY);
    owned_array<uint32_t> all;
    all.append(object);
    if (vtt_argument)
        all.append(vtt_argument);
    for (uint32_t i = 0; i < count; i++)
        all.append(arguments[i]);
    uint32_t n = static_cast<uint32_t>(all.size());
    return make(AST_NODE_CALL, ast_builder_type_basic(AST_TYPE_VOID), callee,
                ast_builder_list(c, all.data(), n), n, 0, 0);
}

/* [stmt.dcl]/2: "Upon each transfer of control (including sequential
   execution of statements) within a function from a point P to a point Q,
   each variable with automatic storage duration that is active at P and
   not at Q is destroyed by the evaluation of its destructor (if any) ...
   Variables with automatic storage duration that are destroyed in this way
   are destroyed in the reverse order of their construction." */
void c_lowering::destroy(size_t mark, owned_array<uint32_t> &out,
                         token_index at)
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    for (size_t i = live.size(); i-- > mark;) {
        const scope_record &r = live[i];
        if (r.kind == scope_record::subobjects) {
            uint32_t destructions = statement(r.symbol);
            if (destructions)
                out.append(destructions);
            continue;
        }
        if (r.kind != scope_record::object)
            continue;
        if (r.bound) {
            /* [class.dtor]/13: "the elements of an array are destroyed in
               reverse order of their construction" */
            uint32_t loop = each_element(r.symbol, r.type, r.element, r.bound,
                                         true, r.destructor, at);
            if (loop)
                out.append(loop);
            continue;
        }
        uint32_t object = make(AST_NODE_VARIABLE, r.type, r.symbol, 0, 0, 0,
                               AST_NODE_FLAG_LVALUE);
        uint32_t call = special_call(
            r.destructor,
            address_of(object, ast_builder_type_pointer(c, r.type)), nullptr, 0,
            at);
        out.append(
            ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0));
    }
}

uint32_t c_lowering::each_element(uint32_t s, ast_qualified_type array,
                                  ast_qualified_type element, uint64_t bound,
                                  bool reverse, entity_index e, token_index at)
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type index_type = ast_builder_type_basic(AST_TYPE_LONG);
    ast_qualified_type truth = ast_builder_type_basic(AST_TYPE_INT);
    uint32_t i = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, index_type,
                                    AST_STORAGE_AUTO, 0, 1);
    auto index = [&] {
        return make(AST_NODE_VARIABLE, index_type, i, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    };
    auto constant = [&](uint64_t value) {
        return ast_builder_integer(c, index_type, static_cast<int64_t>(value));
    };
    ast_qualified_type pointer = ast_builder_type_pointer(c, element);
    uint32_t first =
        cast(make(AST_NODE_VARIABLE, array, s, 0, 0, 0, AST_NODE_FLAG_LVALUE),
             pointer, AST_CAST_ARRAY_DECAY);
    uint32_t object = make(AST_NODE_INDEX, element, first, index(), 0, 0,
                           AST_NODE_FLAG_LVALUE);
    uint32_t call =
        special_call(e, address_of(object, pointer), nullptr, 0, at);
    if (!call)
        return 0;
    uint32_t body =
        ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0);
    uint32_t init, cond, step = 0;
    if (reverse) {
        /* for (i = bound; i-- > 0;) */
        init = make(AST_NODE_ASSIGN, index_type, index(), constant(bound), 0, 0,
                    0);
        cond =
            make(AST_NODE_BINARY, truth,
                 make(AST_NODE_POST_DECREMENT, index_type, index(), 0, 0, 0, 0),
                 constant(0), 0, LEX_TOKEN_GREATER, 0);
    } else {
        /* for (i = 0; i < bound; i++) */
        init = make(AST_NODE_ASSIGN, index_type, index(), constant(0), 0, 0, 0);
        cond = make(AST_NODE_BINARY, truth, index(), constant(bound), 0,
                    LEX_TOKEN_LESS, 0);
        step = make(AST_NODE_POST_INCREMENT, index_type, index(), 0, 0, 0, 0);
    }
    uint32_t step_body[2] = {step, body};
    uint32_t items[2] = {
        ast_builder_node(c, AST_NODE_DECLARATION, v, i, 0, 0, 0, 0),
        ast_builder_node(
            c, AST_NODE_FOR, v,
            ast_builder_node(c, AST_NODE_EXPRESSION, v, init, 0, 0, 0, 0), cond,
            ast_builder_list(c, step_body, 2), 0, 0)};
    return block(items, 2);
}

bool c_lowering::leaving_objects() const
{
    for (const scope_record &r : live)
        if (r.kind == scope_record::object ||
            r.kind == scope_record::subobjects)
            return true;
    return false;
}

size_t c_lowering::leaving(bool is_break) const
{
    for (size_t i = live.size(); i-- > 0;)
        if (live[i].kind == scope_record::loop ||
            (is_break && live[i].kind == scope_record::switch_))
            return i + 1;
    return 0;
}

/* [class.ctor]: a constructor call whose object is a temporary
   ([class.temporary]/1: "Temporary objects are created ... when a prvalue
   is converted to an xvalue", and a prvalue initializing an object is that
   object's initialization, [dcl.init]/17.6.1): GNU C's ({ T t; C(&t, ...);
   t; }) */
uint32_t c_lowering::construct(const semantic_node &n)
{
    if (n.a) {
        /* [class.base.init]: a base or member of *this, constructed in
           place */
        uint32_t object = expression(n.a);
        if (!object)
            return 0;
        uint32_t vtt = 0;
        if (entity_index b = base_subobject_of(n.a))
            vtt = vtt_argument(b, n.at);
        if (failure.what)
            return 0;
        return construct_at(n, object, vtt, n.at);
    }
    bool here = at_result_object;
    if (!in_function)
        return fail("dynamic initialization of a variable", n.at);
    entity_index cls = record(n.type).entity;
    if (analysis.entity_table[cls].destructor && !here)
        /* [class.temporary]/4: destroyed at the end of the full-expression */
        return fail("a temporary with a destructor", n.at);
    ast_qualified_type t = type(n.type, n.at);
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    uint32_t object =
        make(AST_NODE_VARIABLE, t, temporary, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t call = construct_at(n, object, 0, n.at);
    if (!call)
        return 0;
    uint32_t statements[3] = {
        ast_builder_node(c, AST_NODE_DECLARATION, v, temporary, 0, 0, 0, 0),
        ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0),
        ast_builder_node(c, AST_NODE_EXPRESSION, t,
                         make(AST_NODE_VARIABLE, t, temporary, 0, 0, 0,
                              AST_NODE_FLAG_LVALUE),
                         0, 0, 0, 0)};
    return make(AST_NODE_STATEMENT_EXPRESSION, t, block(statements, 3), 0, 0, 0,
                0);
}

/* ---- new and delete ------------------------------------------------ */

/* the value of the semantic node value held in a variable of type t,
   declared and assigned in items (a pointer converted to t): its symbol,
   0 on failure */
uint32_t c_lowering::held_value(uint32_t value, ast_qualified_type t,
                                owned_array<uint32_t> &items)
{
    uint32_t lowered = expression(value);
    if (!lowered)
        return 0;
    if (record(node(value).type).kind == type_kind::pointer)
        lowered = cast(lowered, t, AST_CAST_POINTER_TO_POINTER);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    uint32_t symbol = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t,
                                         AST_STORAGE_AUTO, 0, 1);
    items.append(
        ast_builder_node(c, AST_NODE_DECLARATION, v, symbol, 0, 0, 0, 0));
    uint32_t variable =
        make(AST_NODE_VARIABLE, t, symbol, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    items.append(ast_builder_node(
        c, AST_NODE_EXPRESSION, v,
        make(AST_NODE_ASSIGN, t, variable, lowered, 0, 0, 0), 0, 0, 0, 0));
    return symbol;
}

/* [expr.new]: ({ T *p = (T *)allocation; initialization of *p; p; }), the
   initialization skipped when p is null and the allocation function may
   return null ([expr.new]/20) */
uint32_t c_lowering::new_object(const semantic_node &n)
{
    if (!in_function)
        return fail("a new-expression outside a function", n.at);
    ast_qualified_type t = type(n.type, n.at);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    owned_array<uint32_t> items;
    if (n.d)
        return new_array(n, t);
    uint32_t symbol = held_value(n.a, t, items);
    if (!symbol)
        return 0;
    auto variable = [&] {
        return make(AST_NODE_VARIABLE, t, symbol, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    };
    if (n.b) {
        uint32_t outer = allocated_symbol;
        ast_qualified_type outer_type = allocated_type;
        allocated_symbol = symbol;
        allocated_type = t;
        const semantic_node &init = node(n.b);
        uint32_t done = 0;
        if (init.kind == semantic_kind::construct) {
            done = expression(n.b);
        } else {
            qualified_type object = record(n.type).operand;
            ast_qualified_type u = AST_QUALIFIED_TYPE_UNQUALIFIED(type(
                qualified_type::make(object.unqualified(), cv_none), n.at));
            uint32_t value = 0;
            if (init.kind == semantic_kind::initializer_list) {
                value = compound_literal(n.b);
            } else if (init.kind == semantic_kind::zero) {
                /* [dcl.init]/6: "To zero-initialize an object ... of
                   scalar type T means the object is initialized to the
                   value obtained by converting the integer literal 0 to
                   T" */
                const struct type &r = record(object);
                uint32_t zero = ast_builder_integer(
                    c, ast_builder_type_basic(AST_TYPE_INT), 0);
                if (r.kind == type_kind::pointer)
                    value = cast(zero, u, AST_CAST_NULL_POINTER);
                else if (r.kind == type_kind::fundamental &&
                         fundamental_type_is_floating(r.fundamental))
                    value = cast(zero, u, AST_CAST_INTEGER_TO_FLOAT);
                else
                    value = cast(zero, u, AST_CAST_INTEGER);
            } else {
                value = expression(n.b);
            }
            if (value) {
                uint32_t target =
                    make(AST_NODE_DEREFERENCE, type(object, n.at), variable(),
                         0, 0, 0, AST_NODE_FLAG_LVALUE);
                done = make(AST_NODE_ASSIGN, u, target, value, 0, 0, 0);
            }
        }
        allocated_symbol = outer;
        allocated_type = outer_type;
        if (!done)
            return 0;
        uint32_t statement =
            ast_builder_node(c, AST_NODE_EXPRESSION, v, done, 0, 0, 0, 0);
        if (n.c)
            statement = ast_builder_node(c, AST_NODE_IF, v, truth(variable()),
                                         statement, 0, 0, 0);
        items.append(statement);
    }
    items.append(
        ast_builder_node(c, AST_NODE_EXPRESSION, t, variable(), 0, 0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, t,
                block(items.data(), static_cast<uint32_t>(items.size())), 0, 0,
                0, 0);
}

uint32_t c_lowering::element_loop(uint32_t first, ast_qualified_type object,
                                  ast_qualified_type size, uint32_t count,
                                  bool reverse, uint32_t each)
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type truth = ast_builder_type_basic(AST_TYPE_INT);
    ast_qualified_type pointer = ast_builder_type_pointer(c, object);
    uint32_t i = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, size,
                                    AST_STORAGE_AUTO, 0, 1);
    uint32_t element = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0,
                                          pointer, AST_STORAGE_AUTO, 0, 1);
    auto index = [&] {
        return make(AST_NODE_VARIABLE, size, i, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    };
    auto counted = [&] {
        return make(AST_NODE_VARIABLE, size, count, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    };
    uint32_t outer = allocated_symbol;
    ast_qualified_type outer_type = allocated_type;
    allocated_symbol = element;
    allocated_type = pointer;
    uint32_t done = expression(each);
    allocated_symbol = outer;
    allocated_type = outer_type;
    if (!done)
        return 0;
    uint32_t at_index = make(
        AST_NODE_INDEX, object,
        make(AST_NODE_VARIABLE, pointer, first, 0, 0, 0, AST_NODE_FLAG_LVALUE),
        to_ptrdiff(index()), 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t body_items[2] = {
        ast_builder_node(c, AST_NODE_EXPRESSION, v,
                         make(AST_NODE_ASSIGN, pointer,
                              make(AST_NODE_VARIABLE, pointer, element, 0, 0, 0,
                                   AST_NODE_FLAG_LVALUE),
                              address_of(at_index, pointer), 0, 0, 0),
                         0, 0, 0, 0),
        ast_builder_node(c, AST_NODE_EXPRESSION, v, done, 0, 0, 0, 0)};
    uint32_t body = block(body_items, 2);
    uint32_t init, cond, step = 0;
    if (reverse) {
        /* for (i = count; i-- > 0;) */
        init = make(AST_NODE_ASSIGN, size, index(), counted(), 0, 0, 0);
        cond = make(AST_NODE_BINARY, truth,
                    make(AST_NODE_POST_DECREMENT, size, index(), 0, 0, 0, 0),
                    ast_builder_integer(c, size, 0), 0, LEX_TOKEN_GREATER, 0);
    } else {
        /* for (i = 0; i < count; i++) */
        init = make(AST_NODE_ASSIGN, size, index(),
                    ast_builder_integer(c, size, 0), 0, 0, 0);
        cond = make(AST_NODE_BINARY, truth, index(), counted(), 0,
                    LEX_TOKEN_LESS, 0);
        step = make(AST_NODE_POST_INCREMENT, size, index(), 0, 0, 0, 0);
    }
    uint32_t step_body[2] = {step, body};
    uint32_t items[3] = {
        ast_builder_node(c, AST_NODE_DECLARATION, v, i, 0, 0, 0, 0),
        ast_builder_node(c, AST_NODE_DECLARATION, v, element, 0, 0, 0, 0),
        ast_builder_node(
            c, AST_NODE_FOR, v,
            ast_builder_node(c, AST_NODE_EXPRESSION, v, init, 0, 0, 0, 0), cond,
            ast_builder_list(c, step_body, 2), 0, 0)};
    return block(items, 3);
}

/* a variable symbol declared in items, assigned value if any */
static uint32_t declared_symbol(cc *c, owned_array<uint32_t> &items,
                                ast_qualified_type t)
{
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t,
                                    AST_STORAGE_AUTO, 0, 1);
    items.append(ast_builder_node(c, AST_NODE_DECLARATION,
                                  ast_builder_type_basic(AST_TYPE_VOID), s, 0,
                                  0, 0, 0));
    return s;
}

/* [expr.new]/22 and [abi:array-cookies]: ({ size_t k = count; char *raw =
   allocation; T *p = (T *)raw; if (raw) { *(size_t *)raw = k; p = (T *)(raw
   + cookie); for each element: its initialization; } p; }), the test only
   when the allocation function may return null */
uint32_t c_lowering::new_array(const semantic_node &n, ast_qualified_type t)
{
    if (target == PP_DARWIN && n.value)
        /* Apple's arm64 cookie has the element size too */
        return fail("an array cookie of Apple's arm64 ABI", n.at);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type size =
        AST_QUALIFIED_TYPE_UNQUALIFIED(type(node(n.d).type, n.at));
    ast_qualified_type bytes =
        ast_builder_type_pointer(c, ast_builder_type_basic(AST_TYPE_CHAR));
    qualified_type element = record(n.type).operand;
    ast_qualified_type object = type(element, n.at);
    owned_array<uint32_t> items;
    uint32_t count = held_value(n.d, size, items);
    if (!count)
        return 0;
    uint32_t outer_count = count_symbol;
    count_symbol = count;
    uint32_t raw = held_value(n.a, bytes, items);
    count_symbol = outer_count;
    if (!raw)
        return 0;
    auto variable = [&](uint32_t s, ast_qualified_type st) {
        return make(AST_NODE_VARIABLE, st, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    };
    uint32_t p = declared_symbol(c, items, t);
    items.append(ast_builder_node(
        c, AST_NODE_EXPRESSION, v,
        make(AST_NODE_ASSIGN, t, variable(p, t),
             cast(variable(raw, bytes), t, AST_CAST_POINTER_TO_POINTER), 0, 0,
             0),
        0, 0, 0, 0));
    owned_array<uint32_t> body;
    if (n.value) {
        ast_qualified_type counts = ast_builder_type_pointer(c, size);
        uint32_t stored = make(
            AST_NODE_DEREFERENCE, size,
            cast(variable(raw, bytes), counts, AST_CAST_POINTER_TO_POINTER), 0,
            0, 0, AST_NODE_FLAG_LVALUE);
        body.append(ast_builder_node(
            c, AST_NODE_EXPRESSION, v,
            make(AST_NODE_ASSIGN, size, stored, variable(count, size), 0, 0, 0),
            0, 0, 0, 0));
        body.append(ast_builder_node(
            c, AST_NODE_EXPRESSION, v,
            make(AST_NODE_ASSIGN, t, variable(p, t),
                 offset_pointer(variable(raw, bytes),
                                static_cast<int64_t>(n.value), t),
                 0, 0, 0),
            0, 0, 0, 0));
    }
    if (n.b) {
        uint32_t loop = element_loop(p, object, size, count, false, n.b);
        if (!loop)
            return 0;
        body.append(loop);
    }
    if (!body.empty()) {
        uint32_t made = block(body.data(), static_cast<uint32_t>(body.size()));
        if (n.c)
            made = ast_builder_node(c, AST_NODE_IF, v,
                                    truth(variable(raw, bytes)), made, 0, 0, 0);
        items.append(made);
    }
    items.append(ast_builder_node(c, AST_NODE_EXPRESSION, t, variable(p, t), 0,
                                  0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, t,
                block(items.data(), static_cast<uint32_t>(items.size())), 0, 0,
                0, 0);
}

/* [expr.delete]/6, /7 and [abi:array-cookies]: ({ T *p = operand; if (p) {
   char *raw = (char *)p - cookie; size_t k = *(size_t *)raw; each element,
   last first: its destruction; deallocation (raw, k * size + cookie); } }) */
uint32_t c_lowering::delete_array(const semantic_node &n)
{
    if (target == PP_DARWIN)
        return fail("an array cookie of Apple's arm64 ABI", n.at);
    ast_qualified_type t = type(n.computation, n.at);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type size = ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG);
    ast_qualified_type bytes =
        ast_builder_type_pointer(c, ast_builder_type_basic(AST_TYPE_CHAR));
    qualified_type element = record(n.computation).operand;
    ast_qualified_type object =
        type(qualified_type::make(element.unqualified(), cv_none), n.at);
    owned_array<uint32_t> items;
    uint32_t p = held_value(n.a, t, items);
    if (!p)
        return 0;
    auto variable = [&](uint32_t s, ast_qualified_type st) {
        return make(AST_NODE_VARIABLE, st, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    };
    owned_array<uint32_t> body;
    uint32_t raw = declared_symbol(c, body, bytes);
    body.append(ast_builder_node(
        c, AST_NODE_EXPRESSION, v,
        make(AST_NODE_ASSIGN, bytes, variable(raw, bytes),
             offset_pointer(variable(p, t), -static_cast<int64_t>(n.d), bytes),
             0, 0, 0),
        0, 0, 0, 0));
    uint32_t count = declared_symbol(c, body, size);
    uint32_t stored =
        make(AST_NODE_DEREFERENCE, size,
             cast(variable(raw, bytes), ast_builder_type_pointer(c, size),
                  AST_CAST_POINTER_TO_POINTER),
             0, 0, 0, AST_NODE_FLAG_LVALUE);
    body.append(ast_builder_node(
        c, AST_NODE_EXPRESSION, v,
        make(AST_NODE_ASSIGN, size, variable(count, size), stored, 0, 0, 0), 0,
        0, 0, 0));
    if (n.b) {
        uint32_t loop = element_loop(p, object, size, count, true, n.b);
        if (!loop)
            return 0;
        body.append(loop);
    }
    uint32_t outer = allocated_symbol;
    ast_qualified_type outer_type = allocated_type;
    uint32_t outer_count = count_symbol;
    allocated_symbol = raw;
    allocated_type = bytes;
    count_symbol = count;
    uint32_t deallocated = expression(n.c);
    allocated_symbol = outer;
    allocated_type = outer_type;
    count_symbol = outer_count;
    if (!deallocated)
        return 0;
    body.append(
        ast_builder_node(c, AST_NODE_EXPRESSION, v, deallocated, 0, 0, 0, 0));
    items.append(ast_builder_node(
        c, AST_NODE_IF, v, truth(variable(p, t)),
        block(body.data(), static_cast<uint32_t>(body.size())), 0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, v,
                block(items.data(), static_cast<uint32_t>(items.size())), 0, 0,
                0, 0);
}

/* [expr.new]/9: ({ size_t k = count; k > SIZE_MAX / size ? SIZE_MAX : k *
   size; }): a size no allocation function can give when the product does
   not fit, as clang passes */
uint32_t c_lowering::array_size(const semantic_node &n)
{
    if (n.value == 1 && !n.d)
        return expression(n.a);
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    owned_array<uint32_t> items;
    uint32_t symbol = held_value(n.a, t, items);
    if (!symbol)
        return 0;
    auto variable = [&] {
        return make(AST_NODE_VARIABLE, t, symbol, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    };
    auto constant = [&](uint64_t value) {
        return ast_builder_integer(c, t, static_cast<int64_t>(value));
    };
    /* with an array cookie of d bytes: k > (SIZE_MAX - d) / size */
    uint32_t too_large =
        make(AST_NODE_BINARY, ast_builder_type_basic(AST_TYPE_INT), variable(),
             constant((UINT64_MAX - n.d) / n.value), 0, LEX_TOKEN_GREATER, 0);
    uint32_t product = make(AST_NODE_BINARY, t, variable(), constant(n.value),
                            0, LEX_TOKEN_STAR, 0);
    if (n.d)
        product = make(AST_NODE_BINARY, t, product, constant(n.d), 0,
                       LEX_TOKEN_PLUS, 0);
    uint32_t chosen = make(AST_NODE_CONDITIONAL, t, too_large,
                           constant(UINT64_MAX), product, 0, 0);
    items.append(
        ast_builder_node(c, AST_NODE_EXPRESSION, t, chosen, 0, 0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, t,
                block(items.data(), static_cast<uint32_t>(items.size())), 0, 0,
                0, 0);
}

/* [expr.delete]: ({ T *p = operand; if (p) { p->~T(); deallocation; } }) */
uint32_t c_lowering::delete_object(const semantic_node &n)
{
    if (!in_function)
        return fail("a delete-expression outside a function", n.at);
    if (n.d)
        return delete_array(n);
    ast_qualified_type t = type(n.computation, n.at);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    owned_array<uint32_t> items;
    uint32_t symbol = held_value(n.a, t, items);
    if (!symbol)
        return 0;
    uint32_t outer = allocated_symbol;
    ast_qualified_type outer_type = allocated_type;
    allocated_symbol = symbol;
    allocated_type = t;
    uint32_t body[2];
    uint32_t count = 0;
    uint32_t destroyed = 0, deallocated = 0;
    auto variable = [&] {
        return make(AST_NODE_VARIABLE, t, symbol, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    };
    if (n.value == 1) {
        /* [expr.delete]/3: a virtual destructor, the deleting destructor
           in the slot after the complete object one destroys and
           deallocates the most derived object ([abi:vtable-components]) */
        allocated_symbol = outer;
        allocated_type = outer_type;
        uint32_t callee = virtual_function(node(n.b).b, variable(), n.at, 1);
        if (!callee)
            return 0;
        uint32_t argument = variable();
        uint32_t call = make(AST_NODE_CALL, v, callee,
                             ast_builder_list(c, &argument, 1), 1, 0, 0);
        body[0] = ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0);
        items.append(ast_builder_node(c, AST_NODE_IF, v, truth(variable()),
                                      block(body, 1), 0, 0, 0));
        return make(AST_NODE_STATEMENT_EXPRESSION, v,
                    block(items.data(), static_cast<uint32_t>(items.size())), 0,
                    0, 0, 0);
    }
    if (n.b)
        destroyed = expression(n.b);
    deallocated = expression(n.c);
    allocated_symbol = outer;
    allocated_type = outer_type;
    if ((n.b && !destroyed) || !deallocated)
        return 0;
    if (destroyed)
        body[count++] =
            ast_builder_node(c, AST_NODE_EXPRESSION, v, destroyed, 0, 0, 0, 0);
    body[count++] =
        ast_builder_node(c, AST_NODE_EXPRESSION, v, deallocated, 0, 0, 0, 0);
    items.append(ast_builder_node(c, AST_NODE_IF, v, truth(variable()),
                                  block(body, count), 0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, v,
                block(items.data(), static_cast<uint32_t>(items.size())), 0, 0,
                0, 0);
}

/* ---- expressions ---------------------------------------------------- */

ast_qualified_type c_lowering::type_of(uint32_t n)
{
    return ast_builder_ast(c)->nodes[n].ty;
}

/* a C comparison's or logical operator's int as the bool C++ gives */
uint32_t c_lowering::to_bool(uint32_t int_value)
{
    return cast(int_value, ast_builder_type_basic(AST_TYPE_BOOL),
                AST_CAST_BOOL);
}

/* a value whose truth is tested (the operand of !, && and ||, a
   condition): a conversion to bool tests as its operand does
   ([conv.bool]/1: "A zero value, null pointer value, or null member
   pointer value is converted to false; any other value is converted to
   true", as C's test against 0), so the int C's comparisons and logical
   operators give is tested without being made a bool first */
uint32_t c_lowering::truth(uint32_t value)
{
    if (!value)
        return 0;
    const struct node &x = ast_builder_ast(c)->nodes[value];
    if (x.op == AST_NODE_CAST && x.aux == AST_CAST_BOOL)
        return x.a;
    return value;
}

/* C adds a pointer and an integer of ptrdiff_t's type */
uint32_t c_lowering::to_ptrdiff(uint32_t integer)
{
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(integer)) == d)
        return integer;
    return cast(integer, d, AST_CAST_INTEGER);
}

void c_lowering::address_taken(uint32_t lvalue)
{
    const struct node &n = ast_builder_ast(c)->nodes[lvalue];
    if (n.op == AST_NODE_VARIABLE)
        ast_builder_symbol_flags_add(c, n.a, AST_SYMBOL_FLAG_ADDRESS_TAKEN);
}

uint32_t c_lowering::address_of(uint32_t lvalue, ast_qualified_type pointer)
{
    address_taken(lvalue);
    return make(AST_NODE_ADDRESS, pointer, lvalue, 0, 0, 0, 0);
}

/* a glvalue as a C lvalue, failing when C has none for it (an assignment
   or increment used as an object) */
uint32_t c_lowering::object(uint32_t n)
{
    uint32_t made = expression(n);
    if (!made)
        return 0;
    const struct node &x = ast_builder_ast(c)->nodes[made];
    if (!(x.flags & AST_NODE_FLAG_LVALUE) &&
        record(node(n).type).kind != type_kind::function)
        return fail("an assignment or increment used as an object", node(n).at);
    return made;
}

/* [conv.lval]/3.4: the value of a glvalue: "the value contained in the
   object indicated by the glvalue is the prvalue result".  C reads an
   lvalue where it is used as a value; a comma's or conditional's glvalue
   is read in each operand, and an assignment's value is the one stored */
bool c_lowering::folded_read(const semantic_node &x) const
{
    return x.kind == semantic_kind::entity &&
           x.evaluation == constness::constant &&
           analysis.entity_table[x.a].kind == entity_kind::variable &&
           analysis.entity_table[x.a].usable == constness::constant;
}

uint32_t c_lowering::read(uint32_t n)
{
    const semantic_node &x = node(n);
    ast_qualified_type t = type(x.type, x.at);
    if (x.kind == semantic_kind::comma)
        return make(AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    discard(x.a), read(x.b), 0, 0, 0);
    if (x.kind == semantic_kind::conditional)
        return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    expression(x.a), read(x.b), read(x.c), 0, 0);
    /* [basic.def.odr]/4: a variable usable in constant expressions whose
       value is read is not odr-used; its value, which a static data member
       initialized in its class ([class.static.data]/4) needs, having no
       definition to read */
    if (folded_read(x)) {
        semantic_node value = x;
        value.value = analysis.entity_table[x.a].value;
        return constant(value);
    }
    return expression(n);
}

/* [expr.context]/2: a discarded-value expression, evaluated for its
   effects; a glvalue comma or conditional selects no object then */
uint32_t c_lowering::discard(uint32_t n)
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::comma) {
        uint32_t b = discard(x.b);
        return make(AST_NODE_COMMA, type_of(b), discard(x.a), b, 0, 0, 0);
    }
    if (x.kind == semantic_kind::conditional && is_glvalue(x))
        return make(AST_NODE_CONDITIONAL, ast_builder_type_basic(AST_TYPE_VOID),
                    expression(x.a),
                    cast(discard(x.b), ast_builder_type_basic(AST_TYPE_VOID),
                         AST_CAST_VOID),
                    cast(discard(x.c), ast_builder_type_basic(AST_TYPE_VOID),
                         AST_CAST_VOID),
                    0, 0);
    return expression(n);
}

uint32_t c_lowering::constant(const semantic_node &n)
{
    const struct type &r = record(n.type);
    ast_qualified_type t = type(n.type, n.at);
    if (r.kind == type_kind::pointer ||
        (r.kind == type_kind::fundamental &&
         r.fundamental == fundamental_type::std_nullptr_t))
        return cast(
            ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0),
            AST_QUALIFIED_TYPE_UNQUALIFIED(t), AST_CAST_NULL_POINTER);
    fundamental_type f = r.fundamental;
    if (r.kind == type_kind::enumeration)
        f = analysis.entity_table[r.entity].underlying;
    else if (r.kind != type_kind::fundamental)
        return fail("a constant of this type", n.at);
    if (n.evaluation != constness::constant)
        return fail("a constant whose value analysis does not know", n.at);
    switch (f) {
    case fundamental_type::float_:
    case fundamental_type::double_: {
        double d;
        memcpy(&d, &n.value, sizeof d);
        return ast_builder_floating(c, t, d);
    }
    case fundamental_type::long_double:
    case fundamental_type::float16:
        return fail("a floating constant of this type", n.at);
    default:
        return ast_builder_integer(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                                   static_cast<int64_t>(n.value));
    }
}

/* [lex.string]: an lvalue array of the literal's code units and a '\0' */
uint32_t c_lowering::string_literal(const semantic_node &n)
{
    const token &t = tokens[n.at];
    qualified_type element = record(n.type).operand;
    const struct type &e = record(element);
    uint8_t size = 1;
    uint8_t kind = LEX_TOKEN_STRING;
    switch (e.fundamental) {
    case fundamental_type::char16_t_:
        size = 2;
        kind = LEX_TOKEN_UTF16_STRING;
        break;
    case fundamental_type::char32_t_:
        size = 4;
        kind = LEX_TOKEN_UTF32_STRING;
        break;
    case fundamental_type::wchar_t_:
        size = 4;
        kind = LEX_TOKEN_WIDE_STRING;
        break;
    case fundamental_type::char8_t_:
        kind = LEX_TOKEN_UTF8_STRING;
        break;
    default:
        break;
    }
    const uint32_t *units = tokens.files.phase7().string_code_units();
    owned_array<uint8_t> bytes;
    uint32_t count = t.string_length + 1;
    bytes.resize(static_cast<size_t>(count) * size);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t u = 0;
        if (i < t.string_length)
            u = units[t.string_offset + i];
        /* little-endian, as the targets are */
        for (uint8_t b = 0; b < size; b++)
            bytes[static_cast<size_t>(i) * size + b] =
                static_cast<uint8_t>(u >> (8 * b));
    }
    return ast_builder_string(c, type(element, n.at), bytes.data(), count, size,
                              kind);
}

uint32_t c_lowering::conversion(const semantic_node &n)
{
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    switch (static_cast<conversion_kind>(n.op)) {
    case conversion_kind::lvalue_to_rvalue:
        return read(n.a);
    case conversion_kind::array_to_pointer: {
        uint32_t a = object(n.a);
        address_taken(a);
        return cast(a, t, AST_CAST_ARRAY_DECAY);
    }
    case conversion_kind::function_to_pointer:
        return cast(expression(n.a), t, AST_CAST_FUNCTION_DECAY);
    case conversion_kind::integral:
        return cast(expression(n.a), t, AST_CAST_INTEGER);
    case conversion_kind::floating:
        return cast(expression(n.a), t, AST_CAST_FLOAT_TO_FLOAT);
    case conversion_kind::floating_integral: {
        const struct type &to = record(n.type);
        bool to_floating = to.fundamental == fundamental_type::float_ ||
                           to.fundamental == fundamental_type::double_ ||
                           to.fundamental == fundamental_type::long_double ||
                           to.fundamental == fundamental_type::float16;
        if (to_floating)
            return cast(expression(n.a), t, AST_CAST_INTEGER_TO_FLOAT);
        return cast(expression(n.a), t, AST_CAST_FLOAT_TO_INTEGER);
    }
    case conversion_kind::boolean:
        return cast(expression(n.a), t, AST_CAST_BOOL);
    case conversion_kind::null_pointer: {
        /* [conv.ptr]/1: "A null pointer constant can be converted to a
           pointer type; the result is the null pointer value of that
           type"; the constant has no effect to keep */
        uint32_t zero =
            ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0);
        return cast(zero, t, AST_CAST_NULL_POINTER);
    }
    case conversion_kind::pointer:
    case conversion_kind::qualification_cast:
    case conversion_kind::pointer_cast:
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::derived_to_base:
        if (analysis.entity_table[n.b].virtual_base) {
            /* [class.mi]/4: a virtual base, where the object's virtual
               table says ([abi:vtable-components] vbase offset); in a
               complete object constructor or destructor, *this is a
               complete object whose layout gives the offset */
            entity_index k = analysis.entity_table[n.b].enclosing;
            entity_index v = record(analysis.entity_table[n.b].type).entity;
            uint32_t p = expression(n.a);
            if (!p)
                return 0;
            if ((structor_variant == 1 || structor_variant == 3) &&
                k == structor_class && at_this(n.a))
                return offset_pointer(
                    p, static_cast<int64_t>(analysis.entity_table[n.b].offset),
                    t);
            /* the pointer read three times: a temporary holds one with
               side effects */
            uint32_t first = p, second = 0, third = 0;
            if (side_effect_free(p)) {
                second = expression(n.a);
                third = expression(n.a);
            } else {
                if (!collecting)
                    return fail("a pointer to a virtual base of an expression "
                                "with side effects outside a statement",
                                n.at);
                ast_qualified_type pt = type_of(p);
                uint32_t temporary = full_expression_temporary(pt);
                auto read = [&]() {
                    return make(AST_NODE_VARIABLE, pt, temporary, 0, 0, 0,
                                AST_NODE_FLAG_LVALUE);
                };
                first = make(AST_NODE_COMMA, pt,
                             make(AST_NODE_ASSIGN, pt, read(), p, 0, 0, 0),
                             read(), 0, 0, 0);
                second = read();
                third = read();
            }
            if (node(n.a).kind == semantic_kind::address)
                return virtual_base_address(first, second, k, v, t);
            uint32_t null = cast(
                ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0),
                t, AST_CAST_NULL_POINTER);
            return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                        truth(first),
                        virtual_base_address(second, third, k, v, t), null, 0,
                        0);
        }
        /* [conv.ptr]/3: a base at offset 0 (which analysis makes sure
           of): the same address, a null pointer one too */
        if (analysis.entity_table[n.b].offset) {
            /* an object's address (never null), to an empty base the
               layout moved ([abi:class-types] II): &((char *)p)[offset] */
            int64_t offset =
                static_cast<int64_t>(analysis.entity_table[n.b].offset);
            uint32_t p = expression(n.a);
            if (!p)
                return 0;
            if (node(n.a).kind == semantic_kind::address)
                return offset_pointer(p, offset, t);
            /* [conv.ptr]/3: "The null pointer value is converted to the
               null pointer value of the destination type": p ? p + offset
               : 0, p read twice */
            uint32_t again = 0;
            if (side_effect_free(p)) {
                again = expression(n.a);
            } else {
                /* (e.g. a pointer to the virtual base it is in, read
                   from the virtual table): a temporary holds it */
                if (!collecting)
                    return fail("a pointer to a base at a nonzero offset",
                                n.at);
                ast_qualified_type pt = type_of(p);
                uint32_t temporary = full_expression_temporary(pt);
                uint32_t assigned = make(AST_NODE_ASSIGN, pt,
                                         make(AST_NODE_VARIABLE, pt, temporary,
                                              0, 0, 0, AST_NODE_FLAG_LVALUE),
                                         p, 0, 0, 0);
                p = make(AST_NODE_COMMA, pt, assigned,
                         make(AST_NODE_VARIABLE, pt, temporary, 0, 0, 0,
                              AST_NODE_FLAG_LVALUE),
                         0, 0, 0);
                again = make(AST_NODE_VARIABLE, pt, temporary, 0, 0, 0,
                             AST_NODE_FLAG_LVALUE);
            }
            uint32_t null = cast(
                ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0),
                t, AST_CAST_NULL_POINTER);
            return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                        truth(p), offset_pointer(again, offset, t), null, 0, 0);
        }
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::pointer_to_integral:
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_INTEGER);
    case conversion_kind::integral_to_pointer:
        return cast(expression(n.a), t, AST_CAST_INTEGER_TO_POINTER);
    case conversion_kind::to_void:
        return cast(discard(n.a), ast_builder_type_basic(AST_TYPE_VOID),
                    AST_CAST_VOID);
    }
    return fail("a conversion", n.at);
}

/* the C token of an operator */
int c_operator(preprocessing_op_or_punc op)
{
    switch (preprocessing_op_or_punc_primary(op)) {
    case preprocessing_op_or_punc::PLUS:
        return LEX_TOKEN_PLUS;
    case preprocessing_op_or_punc::MINUS:
        return LEX_TOKEN_MINUS;
    case preprocessing_op_or_punc::STAR:
        return LEX_TOKEN_STAR;
    case preprocessing_op_or_punc::SLASH:
        return LEX_TOKEN_SLASH;
    case preprocessing_op_or_punc::PERCENT:
        return LEX_TOKEN_PERCENT;
    case preprocessing_op_or_punc::CARET:
        return LEX_TOKEN_CARET;
    case preprocessing_op_or_punc::AMPERSAND:
        return LEX_TOKEN_AMPERSAND;
    case preprocessing_op_or_punc::BAR:
        return LEX_TOKEN_PIPE;
    case preprocessing_op_or_punc::LESS_LESS:
        return LEX_TOKEN_LESS_LESS;
    case preprocessing_op_or_punc::GREATER_GREATER:
        return LEX_TOKEN_GREATER_GREATER;
    case preprocessing_op_or_punc::EQUAL_EQUAL:
        return LEX_TOKEN_EQUAL_EQUAL;
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
        return LEX_TOKEN_EXCLAMATION_EQUAL;
    case preprocessing_op_or_punc::LESS:
        return LEX_TOKEN_LESS;
    case preprocessing_op_or_punc::GREATER:
        return LEX_TOKEN_GREATER;
    case preprocessing_op_or_punc::LESS_EQUAL:
        return LEX_TOKEN_LESS_EQUAL;
    case preprocessing_op_or_punc::GREATER_EQUAL:
        return LEX_TOKEN_GREATER_EQUAL;
    case preprocessing_op_or_punc::TILDE:
        return LEX_TOKEN_TILDE;
    case preprocessing_op_or_punc::EXCLAMATION:
        return LEX_TOKEN_EXCLAMATION;
    /* a compound assignment's operator is the binary one */
    case preprocessing_op_or_punc::PLUS_EQUAL:
        return LEX_TOKEN_PLUS;
    case preprocessing_op_or_punc::MINUS_EQUAL:
        return LEX_TOKEN_MINUS;
    case preprocessing_op_or_punc::STAR_EQUAL:
        return LEX_TOKEN_STAR;
    case preprocessing_op_or_punc::SLASH_EQUAL:
        return LEX_TOKEN_SLASH;
    case preprocessing_op_or_punc::PERCENT_EQUAL:
        return LEX_TOKEN_PERCENT;
    case preprocessing_op_or_punc::CARET_EQUAL:
        return LEX_TOKEN_CARET;
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
        return LEX_TOKEN_AMPERSAND;
    case preprocessing_op_or_punc::BAR_EQUAL:
        return LEX_TOKEN_PIPE;
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
        return LEX_TOKEN_LESS_LESS;
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
        return LEX_TOKEN_GREATER_GREATER;
    default:
        return 0;
    }
}

bool is_comparison(int op)
{
    return op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL ||
           op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ||
           op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL;
}

uint32_t c_lowering::binary(const semantic_node &n)
{
    int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
    if (!op)
        return fail("a binary operator", n.at);
    /* [conv.ptr]/1: a null pointer value is distinct from every pointer to
       an object or function: the address of a variable or function that is
       not weak compared with one is known, as clang's code generator folds
       it (the variable is then not referenced) */
    if ((op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL) &&
        ((nonnull_address(n.a) && null_pointer(n.b)) ||
         (null_pointer(n.a) && nonnull_address(n.b))))
        return to_bool(ast_builder_integer(
            c, ast_builder_type_basic(AST_TYPE_INT),
            static_cast<int64_t>(op == LEX_TOKEN_EXCLAMATION_EQUAL)));
    uint32_t a = expression(n.a), b = expression(n.b);
    if (!a || !b)
        return 0;
    if (is_comparison(op))
        return to_bool(make(AST_NODE_BINARY,
                            ast_builder_type_basic(AST_TYPE_INT), a, b, 0,
                            static_cast<uint16_t>(op), 0));
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    /* [expr.add]/4: a pointer and an integer; C has the pointer first and
       the integer as a ptrdiff_t */
    bool a_pointer = record(node(n.a).type).kind == type_kind::pointer;
    bool b_pointer = record(node(n.b).type).kind == type_kind::pointer;
    if (a_pointer && !b_pointer)
        b = to_ptrdiff(b);
    else if (b_pointer && !a_pointer) {
        uint32_t integer = a;
        a = b;
        b = to_ptrdiff(integer);
    }
    return make(AST_NODE_BINARY, t, a, b, 0, static_cast<uint16_t>(op), 0);
}

/*
 * A trivial assignment of a class whose tail padding a derived class may
 * reuse ([abi:pod]): the left operand may be a base class subobject, so
 * only the class's data size is copied, as clang copies it:
 *     *(p = &a, *(struct { char d[dsize]; } *)p = *(... *)&b, p)
 * with p a temporary.
 */
uint32_t c_lowering::data_size_assignment(const semantic_node &n, uint32_t a,
                                          uint32_t b)
{
    if (!collecting)
        return fail("an assignment of a class with tail padding outside a "
                    "statement",
                    n.at);
    if (!(ast_builder_ast(c)->nodes[b].flags & AST_NODE_FLAG_LVALUE))
        /* a prvalue: no object to copy the data size of */
        return fail("an assignment of a class with tail padding from a "
                    "prvalue",
                    n.at);
    const entity &x = analysis.entity_table[record(n.type).entity];
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    ast_qualified_type pointer = ast_builder_type_pointer(c, t);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type view = ast_builder_type_record(c, "", 0, false);
    ast_qualified_type data =
        ast_builder_type_array(c, byte, x.data_size, true);
    const char *field_name = "d";
    size_t field_length = 1;
    uint32_t no_alignment = 0;
    uint8_t not_packed = 0;
    int32_t no_width = -1;
    ast_builder_type_record_complete(c, view, &data, &field_name, &field_length,
                                     &no_alignment, &not_packed, &no_width, 1);
    ast_qualified_type view_pointer = ast_builder_type_pointer(c, view);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    uint32_t temporary = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0,
                                            pointer, AST_STORAGE_AUTO, 0, 1);
    temporaries.append(
        ast_builder_node(c, AST_NODE_DECLARATION, v, temporary, 0, 0, 0, 0));
    auto p = [&] {
        return make(AST_NODE_VARIABLE, pointer, temporary, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    };
    auto as_view = [&](uint32_t address) {
        return make(AST_NODE_DEREFERENCE, view,
                    cast(address, view_pointer, AST_CAST_POINTER_TO_POINTER), 0,
                    0, 0, AST_NODE_FLAG_LVALUE);
    };
    uint32_t stored =
        make(AST_NODE_ASSIGN, pointer, p(), address_of(a, pointer), 0, 0, 0);
    uint32_t copied = make(AST_NODE_ASSIGN, view, as_view(p()),
                           as_view(address_of(b, pointer)), 0, 0, 0);
    uint32_t sequence =
        make(AST_NODE_COMMA, pointer, stored,
             make(AST_NODE_COMMA, pointer, copied, p(), 0, 0, 0), 0, 0, 0);
    return make(AST_NODE_DEREFERENCE, t, sequence, 0, 0, 0,
                AST_NODE_FLAG_LVALUE);
}

uint32_t c_lowering::assignment(const semantic_node &n)
{
    uint32_t a = object(n.a);
    uint32_t b = expression(n.b);
    if (!a || !b)
        return 0;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    if (preprocessing_op_or_punc_primary(static_cast<preprocessing_op_or_punc>(
            n.op)) == preprocessing_op_or_punc::EQUAL) {
        const struct type &r = record(n.type);
        if (r.kind == type_kind::class_type &&
            analysis.entity_table[r.entity].data_size <
                analysis.entity_table[r.entity].size)
            return data_size_assignment(n, a, b);
        return make(AST_NODE_ASSIGN, t, a, b, 0, 0, 0);
    }
    int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
    if (!op)
        return fail("an assignment operator", n.at);
    ast_qualified_type computation = 0;
    if (record(node(n.a).type).kind == type_kind::pointer) {
        computation = t;
        b = to_ptrdiff(b);
    } else
        computation = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.computation, n.at));
    return make(AST_NODE_ASSIGN, t, a, b, computation,
                static_cast<uint16_t>(op), 0);
}

/* [expr.call]: a function called through a pointer; one that returns a
   reference returns its address */
uint32_t c_lowering::call(const semantic_node &n)
{
    bool here = at_result_object;
    qualified_type function_type = node(n.a).type;
    uint32_t callee = 0;
    owned_array<uint32_t> arguments;
    if (n.c) {
        /* [class.virtual]/16: the final overrider, the function pointer in
           the object's virtual table at the function's slot
           ([abi:vtable-components]); the object's address is read twice,
           so it is lowered twice when that has no effect, else held */
        uint32_t object = expression(n.b);
        if (!object)
            return 0;
        if (!side_effect_free(object))
            return fail("a virtual call on an object whose address has "
                        "side effects",
                        n.at);
        uint32_t again = expression(n.b);
        callee = virtual_function(node(n.a).a, again, n.at);
        if (!callee)
            return 0;
        arguments.append(object);
    } else {
        callee = expression(n.a);
        if (!callee)
            return 0;
        if (record(function_type).kind == type_kind::pointer)
            function_type = record(function_type).operand;
        else
            callee = cast(callee, ast_builder_type_pointer(c, type_of(callee)),
                          AST_CAST_FUNCTION_DECAY);
        if (n.b) {
            /* the implicit object argument: the object's address, the
               function's first parameter */
            uint32_t object = expression(n.b);
            if (!object)
                return 0;
            arguments.append(object);
        }
    }
    const uint32_t *items = analysis.semantics.items(n.first);
    const struct type &ft = record(function_type);
    const qualified_type *parameters =
        analysis.type_table.parameters(function_type.unqualified());
    for (uint32_t i = 0; i < n.count; i++) {
        uint32_t a = 0;
        if (i < ft.parameter_count && passed_indirectly(parameters[i]))
            a = indirect_argument(items[i], parameters[i], n.at);
        else if (node(items[i]).kind == semantic_kind::initializer_list)
            /* a class's object initialized by a braced-init-list
               ([dcl.init.list]/3.4): its value */
            a = compound_literal(items[i]);
        else
            a = expression(items[i]);
        if (!a)
            return 0;
        arguments.append(a);
    }
    qualified_type result = record(function_type).operand;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(result, n.at));
    uint32_t count = static_cast<uint32_t>(arguments.size());
    bool initializes = here;
    uint32_t made =
        make(AST_NODE_CALL, t, callee,
             ast_builder_list(c, arguments.data(), count), count, 0, 0);
    if (passed_indirectly(result) && !initializes)
        return materialize(made, result, n.at);
    if (!is_reference(result))
        return made;
    return make(AST_NODE_DEREFERENCE, type(n.type, n.at), made, 0, 0, 0,
                AST_NODE_FLAG_LVALUE);
}

/* [expr.cond] */
uint32_t c_lowering::conditional(const semantic_node &n)
{
    ast_qualified_type t = type(n.type, n.at);
    if (!is_glvalue(n))
        return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    truth(expression(n.a)), expression(n.b), expression(n.c), 0,
                    0);
    /* the object one of the operands is: its address selected */
    ast_qualified_type pointer = ast_builder_type_pointer(c, t);
    uint32_t b = object(n.b), c_ = object(n.c);
    if (!b || !c_)
        return 0;
    uint32_t selected =
        make(AST_NODE_CONDITIONAL, pointer, truth(expression(n.a)),
             address_of(b, pointer), address_of(c_, pointer), 0, 0);
    return make(AST_NODE_DEREFERENCE, t, selected, 0, 0, 0,
                AST_NODE_FLAG_LVALUE);
}

/* the initializer n of an object: a prvalue there initializes the object
   itself ([dcl.init]/17.6.1) */
uint32_t c_lowering::initializer(uint32_t n)
{
    uint32_t outer = result_object;
    result_object = n;
    uint32_t lowered = expression(n);
    result_object = outer;
    return lowered;
}

uint32_t c_lowering::construct_at(const semantic_node &n, uint32_t object,
                                  uint32_t vtt, token_index at)
{
    owned_array<uint32_t> arguments;
    const uint32_t *items = analysis.semantics.items(n.first);
    for (uint32_t i = 0; i < n.count; i++) {
        uint32_t a = expression(items[i]);
        if (!a)
            return 0;
        arguments.append(a);
    }
    return special_call(
        n.b, address_of(object, ast_builder_type_pointer(c, type_of(object))),
        arguments.data(), n.count, at, vtt);
}

entity_index c_lowering::element_destructor(qualified_type type,
                                            qualified_type &object) const
{
    object = type;
    while (record(object).kind == type_kind::array)
        object = record(object).operand;
    if (record(object).kind != type_kind::class_type)
        return 0;
    return analysis.entity_table[record(object).entity].destructor;
}

uint32_t c_lowering::full_expression_temporary(ast_qualified_type t)
{
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    temporaries.append(ast_builder_node(c, AST_NODE_DECLARATION,
                                        ast_builder_type_basic(AST_TYPE_VOID),
                                        temporary, 0, 0, 0, 0));
    return temporary;
}

void c_lowering::destroyed_after(entity_index destructor, uint32_t temporary,
                                 ast_qualified_type t, token_index at)
{
    uint32_t object =
        make(AST_NODE_VARIABLE, t, temporary, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t call = special_call(
        destructor, address_of(object, ast_builder_type_pointer(c, t)), nullptr,
        0, at);
    destructions.append(ast_builder_node(c, AST_NODE_EXPRESSION,
                                         ast_builder_type_basic(AST_TYPE_VOID),
                                         call, 0, 0, 0, 0));
}

/* [class.temporary]/2: "the temporary materialization conversion": a
   prvalue of a class returned by address that initializes no object, held
   in a temporary destroyed at the end of the full-expression; *(t =
   value, &t) */
uint32_t c_lowering::materialize(uint32_t value, qualified_type type_,
                                 token_index at)
{
    entity_index destructor =
        analysis.entity_table[record(type_).entity].destructor;
    if (!collecting)
        return fail("a class returned by address outside a statement", at);
    if (destructor && conditional_depth)
        return fail("a class returned by address in a conditional operand", at);
    ast_qualified_type t = type(type_, at);
    ast_qualified_type pointer = ast_builder_type_pointer(c, t);
    uint32_t temporary = full_expression_temporary(t);
    auto variable = [&] {
        return make(AST_NODE_VARIABLE, t, temporary, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    };
    if (destructor)
        destroyed_after(destructor, temporary, t, at);
    uint32_t stored = make(AST_NODE_ASSIGN, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                           variable(), value, 0, 0, 0);
    uint32_t address = make(AST_NODE_COMMA, pointer, stored,
                            address_of(variable(), pointer), 0, 0, 0);
    return make(AST_NODE_DEREFERENCE, t, address, 0, 0, 0,
                AST_NODE_FLAG_LVALUE);
}

/* the automatic variable a return statement's operand copies (a copy
   constructor's call on it, as [class.copy.elision]/1.1 allows to omit),
   0 if it is not one */
entity_index c_lowering::returned_variable(uint32_t operand) const
{
    if (!operand)
        return 0;
    const semantic_node &n = node(operand);
    uint32_t argument = 0;
    if (n.kind == semantic_kind::conversion &&
        static_cast<conversion_kind>(n.op) == conversion_kind::lvalue_to_rvalue)
        /* a class trivially copyable but non-trivial for the purposes of
           calls (a non-trivial destructor): its copy is the bytes, still
           elided as clang does */
        argument = n.a;
    else if (n.kind != semantic_kind::construct || n.a || n.count != 1)
        return 0;
    else
        argument = analysis.semantics.items(n.first)[0];
    if (node(argument).kind == semantic_kind::bind_reference)
        argument = node(argument).a;
    const semantic_node &named = node(argument);
    if (named.kind != semantic_kind::entity)
        return 0;
    const entity &x = analysis.entity_table[named.a];
    if (x.kind != entity_kind::variable || !x.automatic ||
        x.type.unqualified() != n.type.unqualified() || x.type.cv() ||
        analysis.entity_table[x.enclosing].parameter_scope)
        return 0;
    return named.a;
}

/* [class.copy.elision]/1.1 as clang applies it: every return statement
   names the same variable */
void c_lowering::find_named_return(uint32_t statement, entity_index &found,
                                   bool &mixed) const
{
    if (!statement || statement >= analysis.semantics.size())
        return;
    const semantic_node &n = node(statement);
    if (n.kind < semantic_kind::block || n.kind > semantic_kind::null_statement)
        return;
    if (n.kind == semantic_kind::return_statement) {
        entity_index e = returned_variable(n.a);
        if (!e || (found && found != e))
            mixed = true;
        found = e;
        return;
    }
    if (n.kind == semantic_kind::block ||
        n.kind == semantic_kind::declarations) {
        const uint32_t *items = analysis.semantics.items(n.first);
        for (uint32_t i = 0; i < n.count; i++)
            find_named_return(items[i], found, mixed);
        return;
    }
    if (n.kind == semantic_kind::declaration_statement)
        return;
    find_named_return(n.a, found, mixed);
    find_named_return(n.b, found, mixed);
    find_named_return(n.c, found, mixed);
    find_named_return(n.d, found, mixed);
}

/* [expr.call]/7: "When a function is called, each parameter is
   initialized with its corresponding argument"; [abi:calls]: a class
   passed by address is a temporary the caller constructs, whose address
   is the argument, destroyed at the end of the full-expression
   ([class.temporary]/4; clang's, as Itanium's caller-destroyed rule).
   (construct the temporary, &temporary) */
uint32_t c_lowering::indirect_argument(uint32_t argument,
                                       qualified_type parameter, token_index at)
{
    entity_index cls = record(parameter).entity;
    entity_index destructor = analysis.entity_table[cls].destructor;
    if (!collecting)
        return fail("a class argument by value outside a statement", at);
    if (destructor && conditional_depth)
        /* destroyed only if constructed: clang's flag */
        return fail("a class argument by value in a conditional operand", at);
    ast_qualified_type t = type(parameter, at);
    ast_qualified_type pointer = ast_builder_type_pointer(c, t);
    uint32_t temporary = full_expression_temporary(t);
    uint32_t object =
        make(AST_NODE_VARIABLE, t, temporary, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    const semantic_node &init = node(argument);
    uint32_t made = 0;
    if (init.kind == semantic_kind::construct && !init.a) {
        made = construct_at(init, object, 0, at);
    } else {
        /* a prvalue of the class: its bytes are the object (no copy
           constructor runs, [dcl.init]/17.6.1) */
        uint32_t value = initializer(argument);
        if (!value)
            return 0;
        made = make(AST_NODE_ASSIGN, t, object, value, 0, 0, 0);
    }
    if (!made)
        return 0;
    if (destructor)
        destroyed_after(destructor, temporary, t, at);
    return make(AST_NODE_COMMA, pointer, made, address_of(object, pointer), 0,
                0, 0);
}

uint32_t c_lowering::full_expression(uint32_t e, bool discarded,
                                     owned_array<uint32_t> &before,
                                     owned_array<uint32_t> &after)
{
    bool outer = collecting;
    size_t first = temporaries.size(), first_destruction = destructions.size();
    collecting = true;
    uint32_t lowered = 0;
    if (discarded)
        lowered = discard(e);
    else
        lowered = initializer(e);
    collecting = outer;
    for (size_t i = first; i < temporaries.size(); i++)
        before.append(temporaries[i]);
    /* [class.temporary]/5: destroyed in the reverse order of their
       construction */
    for (size_t i = destructions.size(); i-- > first_destruction;)
        after.append(destructions[i]);
    temporaries.resize(first);
    destructions.resize(first_destruction);
    return lowered;
}

/* [dcl.init.ref]: the address of the object a reference is bound to; a
   prvalue's is a temporary's ([class.temporary]), the enclosing block's
   object as C's compound literal is */
uint32_t c_lowering::bind_reference(const semantic_node &n)
{
    ast_qualified_type pointer = type(n.type, n.at);
    const semantic_node &bound = node(n.a);
    if (is_glvalue(bound)) {
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        return address_of(a, pointer);
    }
    if (!in_function)
        return fail("a temporary bound to a reference of static storage", n.at);
    uint32_t literal = compound_literal(n.a);
    if (!literal)
        return 0;
    return make(AST_NODE_ADDRESS, pointer, literal, 0, 0, 0, 0);
}

/* the prvalue n held in a temporary of the enclosing block, as C's
   compound literal is: an initializer list as an operand */
uint32_t c_lowering::compound_literal(uint32_t n)
{
    const semantic_node &held = node(n);
    if (!in_function)
        return fail("a temporary of static storage", held.at);
    uint32_t value = expression(n);
    if (!value)
        return 0;
    ast_qualified_type t = type(held.type, held.at);
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    ast_builder_symbol_body(c, temporary, value);
    return make(AST_NODE_COMPOUND_LITERAL, t, value, temporary, 0, 0,
                AST_NODE_FLAG_LVALUE);
}

/* [dcl.init.aggr]: an array's elements in order, the rest zero */
uint32_t c_lowering::initializer_list(const semantic_node &n)
{
    if (record(n.type).kind == type_kind::class_type) {
        /* a struct's initializer: a value per field, in order; none (0)
           for an unnamed bit-field, which is no element ([class.bit]/2) */
        owned_array<uint32_t> values;
        const uint32_t *items = analysis.semantics.items(n.first);
        entity_index ce = record(n.type).entity;
        const entity &x = analysis.entity_table[ce];
        class_type(ce, n.at);
        for (uint32_t i = 0, item = 0; item < n.count; i++) {
            if (i >= x.member_count)
                return fail("an initializer for no member", n.at);
            entity_index member = analysis.class_members[x.first_member + i];
            const entity &m = analysis.entity_table[member];
            /* the padding fields before it (class_type) */
            while (values.size() < c_fields[member])
                values.append(0);
            if (m.bit_field && !m.name) {
                values.append(0);
                continue;
            }
            if (tail_reused[member])
                return fail("an initializer of a base whose tail padding "
                            "is reused",
                            n.at);
            uint32_t e = expression(items[item++]);
            if (!e)
                return 0;
            values.append(e);
        }
        uint32_t value_count = static_cast<uint32_t>(values.size());
        /* a union's: the value of field a (C's aux 2), the first */
        uint16_t aux = 0;
        if (analysis.entity_table[record(n.type).entity].union_)
            aux = 2;
        return make(AST_NODE_INITIALIZER, type(n.type, n.at), 0,
                    ast_builder_list(c, values.data(), value_count),
                    value_count, aux, 0);
    }
    owned_array<uint32_t> runs;
    const uint32_t *items = analysis.semantics.items(n.first);
    for (uint32_t i = 0; i < n.count; i++) {
        uint32_t e = expression(items[i]);
        if (!e)
            return 0;
        runs.append(i);
        runs.append(i);
        runs.append(e);
    }
    return make(AST_NODE_INITIALIZER, type(n.type, n.at), 0,
                ast_builder_list(c, runs.data(), runs.size()), n.count, 1, 0);
}

uint32_t c_lowering::expression(uint32_t index)
{
    if (failure.what)
        return 0;
    at_result_object = index == result_object;
    if (at_result_object)
        result_object = 0;
    const semantic_node &n = node(index);
    switch (n.kind) {
    case semantic_kind::constant:
        return constant(n);
    case semantic_kind::string_literal:
        return string_literal(n);
    case semantic_kind::entity: {
        entity_index e = n.a;
        const entity &x = analysis.entity_table[e];
        uint32_t s = symbol(e, n.at);
        if (!s)
            return 0;
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
        if (x.kind == entity_kind::function)
            return make(AST_NODE_VARIABLE, type(x.type, n.at), s, 0, 0, 0, 0);
        if (indirect_parameter(e))
            /* the object at the address the caller passed */
            return make(AST_NODE_DEREFERENCE, type(x.type, n.at),
                        make(AST_NODE_VARIABLE, parameter_type(x.type, n.at), s,
                             0, 0, 0, AST_NODE_FLAG_LVALUE),
                        0, 0, 0, AST_NODE_FLAG_LVALUE);
        ast_qualified_type t = type(x.type, n.at);
        uint32_t v =
            make(AST_NODE_VARIABLE, t, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
        if (!is_reference(x.type))
            return v;
        /* [dcl.ref]: a name of a reference is the object it refers to */
        uint8_t flags = AST_NODE_FLAG_LVALUE;
        if (record(n.type).kind == type_kind::function)
            flags = 0;
        return make(AST_NODE_DEREFERENCE, type(n.type, n.at), v, 0, 0, 0,
                    flags);
    }
    case semantic_kind::conversion:
        return conversion(n);
    case semantic_kind::unary: {
        uint32_t a = expression(n.a);
        if (!a)
            return 0;
        int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
        if (op == LEX_TOKEN_EXCLAMATION)
            return to_bool(make(AST_NODE_UNARY,
                                ast_builder_type_basic(AST_TYPE_INT), truth(a),
                                0, 0, LEX_TOKEN_EXCLAMATION, 0));
        if (!op)
            return fail("a unary operator", n.at);
        return make(AST_NODE_UNARY,
                    AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at)), a, 0, 0,
                    static_cast<uint16_t>(op), 0);
    }
    case semantic_kind::address: {
        if (!is_glvalue(node(n.a)))
            /* the implicit object argument of a prvalue: [basic.lval]/7
               the temporary it is materialized into */
            return bind_reference(n);
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        return address_of(a, type(n.type, n.at));
    }
    case semantic_kind::indirection: {
        uint32_t a = expression(n.a);
        if (!a)
            return 0;
        uint8_t flags = AST_NODE_FLAG_LVALUE;
        if (record(n.type).kind == type_kind::function)
            flags = 0;
        return make(AST_NODE_DEREFERENCE, type(n.type, n.at), a, 0, 0, 0,
                    flags);
    }
    case semantic_kind::increment: {
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        int op = AST_NODE_PRE_INCREMENT;
        switch (static_cast<increment_kind>(n.op)) {
        case increment_kind::pre_increment:
            op = AST_NODE_PRE_INCREMENT;
            break;
        case increment_kind::pre_decrement:
            op = AST_NODE_PRE_DECREMENT;
            break;
        case increment_kind::post_increment:
            op = AST_NODE_POST_INCREMENT;
            break;
        case increment_kind::post_decrement:
            op = AST_NODE_POST_DECREMENT;
            break;
        }
        return make(op, AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(a)), a, 0, 0, 0,
                    0);
    }
    case semantic_kind::binary:
        return binary(n);
    case semantic_kind::logical_and:
    case semantic_kind::logical_or: {
        uint32_t a = expression(n.a);
        conditional_depth++;
        uint32_t b = expression(n.b);
        conditional_depth--;
        if (!a || !b)
            return 0;
        int op = AST_NODE_LOGICAL_AND;
        if (n.kind == semantic_kind::logical_or)
            op = AST_NODE_LOGICAL_OR;
        return to_bool(make(op, ast_builder_type_basic(AST_TYPE_INT), truth(a),
                            truth(b), 0, 0, 0));
    }
    case semantic_kind::assignment:
        return assignment(n);
    case semantic_kind::conditional: {
        conditional_depth++;
        uint32_t lowered = conditional(n);
        conditional_depth--;
        return lowered;
    }
    case semantic_kind::comma: {
        ast_qualified_type t = type(n.type, n.at);
        uint32_t a = discard(n.a);
        if (!is_glvalue(n))
            return make(AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(t), a,
                        expression(n.b), 0, 0, 0);
        ast_qualified_type pointer = ast_builder_type_pointer(c, t);
        uint32_t b = object(n.b);
        if (!b)
            return 0;
        uint32_t selected =
            make(AST_NODE_COMMA, pointer, a, address_of(b, pointer), 0, 0, 0);
        return make(AST_NODE_DEREFERENCE, t, selected, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    }
    case semantic_kind::call:
        return call(n);
    case semantic_kind::subscript: {
        uint32_t a = expression(n.a), b = expression(n.b);
        if (!a || !b)
            return 0;
        return make(AST_NODE_INDEX, type(n.type, n.at), a, to_ptrdiff(b), 0, 0,
                    AST_NODE_FLAG_LVALUE);
    }
    case semantic_kind::member: {
        /* [expr.ref]/6.2: the member subobject */
        uint32_t object = expression(n.a);
        if (!object)
            return 0;
        const entity &x = analysis.entity_table[n.b];
        if (!is_reference(x.type) || is_reference(n.type))
            /* a reference member being initialized ([class.base.init]) is
               the pointer itself */
            return ast_builder_member(c, object, c_fields[n.b],
                                      type(n.type, n.at));
        /* [expr.ref]/6.1: a reference member is the object it refers to */
        uint32_t pointer =
            ast_builder_member(c, object, c_fields[n.b], type(x.type, n.at));
        return make(AST_NODE_DEREFERENCE, type(n.type, n.at), pointer, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    }
    case semantic_kind::zero:
        return make(AST_NODE_ZERO, type(n.type, n.at), 0, 0, 0, 0, 0);
    case semantic_kind::initializer_list:
        return initializer_list(n);
    case semantic_kind::bind_reference:
        return bind_reference(n);
    case semantic_kind::construct:
        return construct(n);
    case semantic_kind::new_object:
        return new_object(n);
    case semantic_kind::array_size:
        return array_size(n);
    case semantic_kind::delete_object:
        return delete_object(n);
    case semantic_kind::array_count:
        if (!count_symbol)
            return fail("an array count outside new or delete", n.at);
        return make(AST_NODE_VARIABLE, type(n.type, n.at), count_symbol, 0, 0,
                    0, AST_NODE_FLAG_LVALUE);
    case semantic_kind::allocated:
        if (!allocated_symbol)
            return fail("an allocated pointer outside new or delete", n.at);
        return make(AST_NODE_VARIABLE, allocated_type, allocated_symbol, 0, 0,
                    0, AST_NODE_FLAG_LVALUE);
    case semantic_kind::vtable_pointer_store:
        return vtable_pointer_stores(n);
    case semantic_kind::destroy: {
        uint32_t object = expression(n.a);
        if (!object)
            return 0;
        uint32_t vtt = 0;
        if (entity_index b = base_subobject_of(n.a))
            vtt = vtt_argument(b, n.at);
        if (failure.what)
            return 0;
        return special_call(
            n.b,
            address_of(object, ast_builder_type_pointer(c, type_of(object))),
            nullptr, 0, n.at, vtt);
    }
    default:
        return fail("an expression of this kind", n.at);
    }
}

/* ---- statements ----------------------------------------------------- */

uint32_t c_lowering::block(const uint32_t *items, uint32_t count)
{
    return ast_builder_node(c, AST_NODE_BLOCK,
                            ast_builder_type_basic(AST_TYPE_VOID), 0,
                            ast_builder_list(c, items, count), count, 0, 0);
}

/* a block's statements; a declaration of several variables is one
   statement per variable, in the block's scope */
void c_lowering::block_items(uint32_t first, uint32_t count,
                             owned_array<uint32_t> &out)
{
    const uint32_t *items = analysis.semantics.items(first);
    for (uint32_t i = 0; i < count; i++) {
        const semantic_node &s = node(items[i]);
        if (s.kind == semantic_kind::declarations) {
            block_items(s.first, s.count, out);
            continue;
        }
        if (s.kind == semantic_kind::declaration_statement) {
            declaration_items(s, out);
            continue;
        }
        out.append(statement(items[i]));
    }
}

/* a variable declared in a block, and its constructor's call; one with a
   destructor is live until the block ends ([stmt.dcl]/2) */
void c_lowering::declaration_items(const semantic_node &n,
                                   owned_array<uint32_t> &out)
{
    /* the initializer is a full-expression ([intro.execution]/5.3): the
       temporaries of its arguments before the declaration, destroyed
       after it */
    bool outer = collecting;
    size_t first = temporaries.size(), first_destruction = destructions.size();
    collecting = true;
    owned_array<uint32_t> items;
    initialized_declaration(n, items);
    collecting = outer;
    for (size_t i = first; i < temporaries.size(); i++)
        out.append(temporaries[i]);
    for (uint32_t i : items)
        out.append(i);
    for (size_t i = destructions.size(); i-- > first_destruction;)
        out.append(destructions[i]);
    temporaries.resize(first);
    destructions.resize(first_destruction);
}

void c_lowering::initialized_declaration(const semantic_node &n,
                                         owned_array<uint32_t> &out)
{
    entity_index e = n.a;
    const entity &x = analysis.entity_table[e];
    qualified_type object;
    entity_index destructor = element_destructor(x.type, object);
    uint64_t bound = 0;
    if (!(object == x.type)) {
        const struct type &array = record(x.type);
        if (array.operand == object && array.bound_known)
            bound = array.bound;
    }
    if (!x.automatic && (destructor || (n.b && !constant_initializer(n.b)))) {
        /* [stmt.dcl]/4: initialized the first time control passes
           through, under a guard */
        guarded_initialization(n, destructor, out);
        return;
    }
    if (destructor && !(object == x.type) && !bound) {
        /* an array of more than one dimension */
        fail("an array with a destructor", n.at);
        return;
    }
    if (!n.b || node(n.b).kind != semantic_kind::construct || !x.automatic) {
        /* a block's object, whose destructor is called below */
        out.append(declaration(n, true));
    } else {
        const semantic_node &init = node(n.b);
        uint32_t s = symbol(e, n.at);
        if (!s)
            return;
        owned_array<uint32_t> arguments;
        const uint32_t *items = analysis.semantics.items(init.first);
        for (uint32_t i = 0; i < init.count; i++) {
            uint32_t a = expression(items[i]);
            if (!a)
                return;
            arguments.append(a);
        }
        ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
        ast_qualified_type t = type(x.type, n.at);
        out.append(ast_builder_node(c, AST_NODE_DECLARATION, v, s, 0, 0, 0, 0));
        if (bound) {
            /* [dcl.init]/7.2: each element, first to last */
            uint32_t loop = each_element(s, t, type(object, n.at), bound, false,
                                         init.b, n.at);
            if (!loop)
                return;
            out.append(loop);
        }
        uint32_t variable =
            make(AST_NODE_VARIABLE, t, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
        uint32_t call = 0;
        if (!bound)
            call = special_call(
                init.b, address_of(variable, ast_builder_type_pointer(c, t)),
                arguments.data(), init.count, n.at);
        if (call)
            out.append(
                ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0));
        else if (!bound)
            return;
    }
    if (destructor && e != named_return) {
        scope_record r;
        r.kind = scope_record::object;
        r.symbol = symbol(e, n.at);
        r.type = type(x.type, n.at);
        r.destructor = destructor;
        if (bound) {
            r.element = type(object, n.at);
            r.bound = bound;
        }
        live.append(r);
    }
}

/* a block: its statements, then the destructor calls of the objects it
   declared ([stmt.dcl]/2); prefix (if any) first */
uint32_t c_lowering::scoped_block(const semantic_node &n, uint32_t prefix)
{
    size_t mark = live.size();
    owned_array<uint32_t> items;
    if (prefix)
        items.append(prefix);
    block_items(n.first, n.count, items);
    destroy(mark, items, n.at);
    live.resize(mark);
    return block(items.data(), static_cast<uint32_t>(items.size()));
}

/* the substatement of an iteration or switch statement, which a break (and
   for a loop, a continue) leaves */
uint32_t c_lowering::loop_body(uint32_t n, bool is_switch)
{
    size_t mark = live.size();
    scope_record r;
    r.kind = scope_record::loop;
    if (is_switch)
        r.kind = scope_record::switch_;
    live.append(r);
    uint32_t s = statement(n);
    live.resize(mark);
    return s;
}

/* whether the glvalue n designates an object or function of static
   storage duration ([basic.stc.static]) at an address known before the
   program runs: a variable's, a function, a string literal, or a part of
   one at a constant place */
bool c_lowering::static_lvalue(uint32_t n) const
{
    const semantic_node &x = node(n);
    switch (x.kind) {
    case semantic_kind::string_literal:
        return true;
    case semantic_kind::entity: {
        const entity &e = analysis.entity_table[x.a];
        if (is_reference(e.type))
            return false;
        return e.kind == entity_kind::function ||
               (e.kind == entity_kind::variable && !e.automatic &&
                !e.nonstatic_member);
    }
    case semantic_kind::member:
        return static_lvalue(x.a);
    case semantic_kind::subscript:
        return address_constant(x.a) && constant_initializer(x.b) &&
               node(x.b).kind == semantic_kind::constant;
    case semantic_kind::indirection:
        return address_constant(x.a);
    default:
        return false;
    }
}

/* [expr.const]/11: "a prvalue core constant expression of pointer type ...
   contains the address of an object with static storage duration, ... the
   address of a non-immediate function, or a null pointer value": an
   address the linker resolves */
bool c_lowering::address_constant(uint32_t n) const
{
    const semantic_node &x = node(n);
    switch (x.kind) {
    case semantic_kind::address:
        return static_lvalue(x.a);
    case semantic_kind::conversion:
        switch (static_cast<conversion_kind>(x.op)) {
        case conversion_kind::array_to_pointer:
        case conversion_kind::function_to_pointer:
            return static_lvalue(x.a);
        case conversion_kind::pointer:
        case conversion_kind::qualification_cast:
        case conversion_kind::pointer_cast:
            return address_constant(x.a);
        case conversion_kind::derived_to_base:
            /* a virtual base's offset is read from the virtual table */
            return !analysis.entity_table[x.b].virtual_base &&
                   address_constant(x.a);
        default:
            return false;
        }
    case semantic_kind::binary: {
        /* E1 + E2, E1 - E2 of a pointer and a constant integer */
        if (record(x.type).kind != type_kind::pointer)
            return false;
        const semantic_node &a = node(x.a), &b = node(x.b);
        if (record(a.type).kind == type_kind::pointer)
            return address_constant(x.a) && b.kind == semantic_kind::constant;
        return address_constant(x.b) && a.kind == semantic_kind::constant;
    }
    default:
        return false;
    }
}

/* the address of a variable of static storage duration (or a member of
   one) or of a function, none of them weak: never a null pointer, and
   evaluating it has no effect */
bool c_lowering::nonnull_address(uint32_t n) const
{
    const semantic_node &x = node(n);
    uint32_t object = 0;
    if (x.kind == semantic_kind::address)
        object = x.a;
    else if (x.kind == semantic_kind::conversion) {
        switch (static_cast<conversion_kind>(x.op)) {
        case conversion_kind::array_to_pointer:
        case conversion_kind::function_to_pointer:
            object = x.a;
            break;
        case conversion_kind::pointer:
        case conversion_kind::qualification_cast:
            return nonnull_address(x.a);
        default:
            return false;
        }
    } else {
        return false;
    }
    while (node(object).kind == semantic_kind::member)
        object = node(object).a;
    const semantic_node &o = node(object);
    if (o.kind != semantic_kind::entity)
        return false;
    const entity &e = analysis.entity_table[o.a];
    if (is_reference(e.type) || (e.gnu_flags & gnu_weak))
        return false;
    return e.kind == entity_kind::function ||
           (e.kind == entity_kind::variable && !e.automatic &&
            !e.nonstatic_member);
}

/* a null pointer constant converted to a pointer ([conv.ptr]/1) */
bool c_lowering::null_pointer(uint32_t n) const
{
    const semantic_node &x = node(n);
    return x.kind == semantic_kind::conversion &&
           static_cast<conversion_kind>(x.op) == conversion_kind::null_pointer;
}

/* whether evaluating n reads or does something only the running program
   has: a call, a constructor, an assignment, a variable's value.  An
   initializer without any that analysis could not fold (clang folds an
   out-of-range shift, say) is not lowered as dynamic initialization */
bool c_lowering::needs_run_time(uint32_t n) const
{
    if (!n)
        return false;
    const semantic_node &x = node(n);
    switch (x.kind) {
    case semantic_kind::call:
    case semantic_kind::construct:
    case semantic_kind::assignment:
    case semantic_kind::increment:
        return true;
    case semantic_kind::conversion:
        if (static_cast<conversion_kind>(x.op) ==
                conversion_kind::lvalue_to_rvalue &&
            !constant_initializer(n))
            return true;
        break;
    default:
        break;
    }
    if (x.kind == semantic_kind::entity || x.kind == semantic_kind::member)
        return false;
    if (needs_run_time(x.a) || needs_run_time(x.b) || needs_run_time(x.c) ||
        needs_run_time(x.d))
        return true;
    const uint32_t *items = analysis.semantics.items(x.first);
    for (uint32_t i = 0; i < x.count; i++)
        if (needs_run_time(items[i]))
            return true;
    return false;
}

/* whether an initializer is a constant C emits as data */
bool c_lowering::constant_initializer(uint32_t n) const
{
    const semantic_node &x = node(n);
    switch (x.kind) {
    case semantic_kind::constant:
    case semantic_kind::zero:
    case semantic_kind::string_literal:
        return true;
    case semantic_kind::initializer_list: {
        const uint32_t *items = analysis.semantics.items(x.first);
        for (uint32_t i = 0; i < x.count; i++)
            if (!constant_initializer(items[i]))
                return false;
        return true;
    }
    case semantic_kind::conversion:
        if (address_constant(n))
            return true;
        /* a read of a variable is code, unless read() folds it */
        return x.evaluation == constness::constant &&
               (static_cast<conversion_kind>(x.op) !=
                    conversion_kind::lvalue_to_rvalue ||
                folded_read(node(x.a)));
    case semantic_kind::bind_reference:
        /* a reference bound to an object of static storage duration */
        return static_lvalue(x.a);
    default:
        return x.evaluation == constness::constant || address_constant(n);
    }
}

/* a variable declared in a block: an automatic one is a C declaration, a
   static one's symbol holds its initializer */
uint32_t c_lowering::declaration(const semantic_node &n, bool in_block)
{
    entity_index e = n.a;
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, n.at);
    if (!s)
        return 0;
    if (n.b && x.automatic && node(n.b).kind == semantic_kind::construct)
        return fail("a constructor call in a condition or init-statement",
                    n.at);
    qualified_type object = x.type;
    if (!in_block && record(object).kind == type_kind::class_type &&
        analysis.entity_table[record(object).entity].destructor)
        return fail("an object with a destructor in a condition or "
                    "init-statement",
                    n.at);
    uint32_t init = 0;
    if (n.b) {
        if (!x.automatic && !constant_initializer(n.b))
            return fail("a static local with dynamic initialization", n.at);
        init = initializer(n.b);
        if (!init)
            return 0;
    }
    if (!x.automatic) {
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
        /* [impl.clang]: a static local is emitted with its function,
           referred to or not; an inline function's only where the
           function is (not yet: its function's COMDAT group) */
        if (!analysis.entity_table[enclosing_function(e)].inline_)
            ast_builder_symbol_keep(c, s);
        ast_builder_symbol_body(c, s, init);
        return ast_builder_node(c, AST_NODE_EXPRESSION,
                                ast_builder_type_basic(AST_TYPE_VOID), 0, 0, 0,
                                0, 0);
    }
    return ast_builder_node(c, AST_NODE_DECLARATION,
                            ast_builder_type_basic(AST_TYPE_VOID), s, init, 0,
                            0, 0);
}

/* a condition's value; a condition that declares gives its declaration in
   declaration_statement (0 if none) */
uint32_t c_lowering::condition(uint32_t n, uint32_t &declaration_statement)
{
    declaration_statement = 0;
    const semantic_node &x = node(n);
    if (x.kind != semantic_kind::condition_declaration)
        return truth(expression(n));
    declaration_statement = declaration(node(x.a));
    return truth(expression(x.b));
}

uint32_t c_lowering::label(uint32_t identifier)
{
    for (const label_record &l : labels)
        if (l.identifier == identifier)
            return l.label;
    size_t length = 0;
    const char *s = tokens.pp.identifier_spelling(identifier - 1, &length);
    label_record made;
    made.identifier = identifier;
    made.label = ast_builder_label(c, s, length);
    labels.append(made);
    return made.label;
}

uint32_t c_lowering::statement(uint32_t index)
{
    if (failure.what || !index)
        return 0;
    const semantic_node &n = node(index);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    switch (n.kind) {
    case semantic_kind::block:
    case semantic_kind::declarations:
        return scoped_block(n, 0);
    case semantic_kind::expression_statement: {
        if ((n.d == semantic_analysis::complete_object_only &&
             structor_variant == 2) ||
            (n.d != semantic_analysis::complete_object_only &&
             structor_variant == 3))
            /* [class.base.init]/7: a virtual base is initialized by the
               most derived class's constructor alone (and destroyed by its
               destructor, which runs the rest as the base object one,
               variant 3) */
            return ast_builder_node(c, AST_NODE_EXPRESSION, v, 0, 0, 0, 0, 0);
        owned_array<uint32_t> items, after;
        uint32_t e = full_expression(n.a, true, items, after);
        if (!e)
            return 0;
        uint32_t statement_node =
            ast_builder_node(c, AST_NODE_EXPRESSION, v, e, 0, 0, 0, 0);
        if (items.empty() && after.empty())
            return statement_node;
        items.append(statement_node);
        for (uint32_t a : after)
            items.append(a);
        return block(items.data(), static_cast<uint32_t>(items.size()));
    }
    case semantic_kind::null_statement:
        return ast_builder_node(c, AST_NODE_EXPRESSION, v, 0, 0, 0, 0, 0);
    case semantic_kind::declaration_statement: {
        /* a substatement that is a declaration: [stmt.pre]/2 its own block
           scope */
        size_t mark = live.size();
        owned_array<uint32_t> items;
        declaration_items(n, items);
        destroy(mark, items, n.at);
        live.resize(mark);
        return block(items.data(), static_cast<uint32_t>(items.size()));
    }
    case semantic_kind::if_statement: {
        /* [stmt.if]/3: an init-statement is the statement before the if, in
           a block around both; a condition that declares likewise
           ([stmt.select]: its scope is the if statement) */
        uint32_t declared = 0;
        uint32_t init = statement(n.d);
        uint32_t cond = condition(n.a, declared);
        uint32_t made = ast_builder_node(c, AST_NODE_IF, v, cond,
                                         statement(n.b), statement(n.c), 0, 0);
        uint32_t items[3];
        uint32_t count = 0;
        if (init)
            items[count++] = init;
        if (declared)
            items[count++] = declared;
        if (!count)
            return made;
        items[count++] = made;
        return block(items, count);
    }
    case semantic_kind::while_statement: {
        uint32_t declared = 0;
        uint32_t cond = condition(n.a, declared);
        uint32_t body = loop_body(n.b, false);
        if (!declared)
            return ast_builder_node(c, AST_NODE_WHILE, v, cond, body, 0, 0, 0);
        /* [stmt.while]/2: with a declaration the loop is
               label: { if (condition) { statement goto label; } }
           here a for without a condition whose body declares, tests and
           leaves */
        uint32_t leave = ast_builder_node(c, AST_NODE_BREAK, v, 0, 0, 0, 0, 0);
        uint32_t test =
            ast_builder_node(c, AST_NODE_IF, v, cond, body, leave, 0, 0);
        uint32_t items[2] = {declared, test};
        uint32_t step_body[2] = {0, block(items, 2)};
        return ast_builder_node(c, AST_NODE_FOR, v, 0, 0,
                                ast_builder_list(c, step_body, 2), 0, 0);
    }
    case semantic_kind::do_statement: {
        uint32_t body = loop_body(n.a, false);
        return ast_builder_node(c, AST_NODE_DO, v, truth(expression(n.b)), body,
                                0, 0, 0);
    }
    case semantic_kind::for_statement: {
        uint32_t init = 0;
        size_t mark = live.size();
        if (n.a) {
            const semantic_node &first = node(n.a);
            if (first.kind == semantic_kind::declaration_statement ||
                first.kind == semantic_kind::declarations) {
                owned_array<uint32_t> items;
                if (first.kind == semantic_kind::declarations)
                    block_items(first.first, first.count, items);
                else
                    items.append(declaration(first));
                init = block(items.data(), static_cast<uint32_t>(items.size()));
            } else if (first.kind != semantic_kind::null_statement)
                init = statement(n.a);
        }
        if (live.size() != mark)
            return fail("an object with a destructor in an init-statement",
                        n.at);
        uint32_t declared = 0, cond = 0;
        if (n.b)
            cond = condition(n.b, declared);
        uint32_t step = 0;
        if (n.c)
            step = discard(n.c);
        uint32_t body = loop_body(n.d, false);
        if (declared) {
            uint32_t leave =
                ast_builder_node(c, AST_NODE_BREAK, v, 0, 0, 0, 0, 0);
            uint32_t test =
                ast_builder_node(c, AST_NODE_IF, v, cond, body, leave, 0, 0);
            uint32_t items[2] = {declared, test};
            body = block(items, 2);
            cond = 0;
        }
        uint32_t step_body[2] = {step, body};
        return ast_builder_node(c, AST_NODE_FOR, v, init, cond,
                                ast_builder_list(c, step_body, 2), 0, 0);
    }
    case semantic_kind::switch_statement: {
        /* [stmt.switch]/7: an init-statement is before the switch, in a
           block around both */
        uint32_t declared = 0;
        uint32_t init = statement(n.d);
        uint32_t cond = condition(n.a, declared);
        uint32_t made = ast_builder_node(c, AST_NODE_SWITCH, v, cond,
                                         loop_body(n.b, true), 0, 0, 0);
        uint32_t items[3];
        uint32_t count = 0;
        if (init)
            items[count++] = init;
        if (declared)
            items[count++] = declared;
        if (!count)
            return made;
        items[count++] = made;
        return block(items, count);
    }
    case semantic_kind::case_label:
        return ast_builder_case(c, static_cast<int64_t>(n.value),
                                statement(n.a));
    case semantic_kind::default_label:
        return ast_builder_node(c, AST_NODE_DEFAULT, v, 0, 0, statement(n.a), 0,
                                0);
    case semantic_kind::identifier_label: {
        uint32_t l = label(static_cast<uint32_t>(n.value));
        return ast_builder_label_define(c, l, statement(n.a));
    }
    case semantic_kind::goto_statement:
        if (leaving_objects())
            /* [stmt.dcl]/2's destruction on a jump out of the objects'
               scopes, which a goto needs its target's scope for */
            return fail("a goto where an object with a destructor is live",
                        n.at);
        return ast_builder_node(c, AST_NODE_GOTO, v,
                                label(static_cast<uint32_t>(n.value)), 0, 0, 0,
                                0);
    case semantic_kind::break_statement:
    case semantic_kind::continue_statement: {
        bool is_break = n.kind == semantic_kind::break_statement;
        int op = AST_NODE_CONTINUE;
        if (is_break)
            op = AST_NODE_BREAK;
        uint32_t jump = ast_builder_node(c, op, v, 0, 0, 0, 0, 0);
        size_t mark = leaving(is_break);
        if (mark >= live.size())
            return jump;
        owned_array<uint32_t> items;
        destroy(mark, items, n.at);
        items.append(jump);
        return block(items.data(), static_cast<uint32_t>(items.size()));
    }
    case semantic_kind::return_statement: {
        uint32_t value = 0;
        owned_array<uint32_t> items, after;
        if (n.a && named_return && returned_variable(n.a) == named_return) {
            /* the variable is the result object: its bytes are the
               result, and it is not destroyed */
            uint32_t s = symbol(named_return, n.at);
            value = make(AST_NODE_VARIABLE,
                         type(analysis.entity_table[named_return].type, n.at),
                         s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
        } else if (n.a && node(n.a).kind == semantic_kind::initializer_list) {
            /* a class's result initialized by a braced-init-list
               ([stmt.return]/2, [dcl.init.list]/3.4): its value */
            value = compound_literal(n.a);
            if (!value)
                return 0;
        } else if (n.a) {
            value = full_expression(n.a, false, items, after);
            if (!value)
                return 0;
        }
        if (!leaving_objects() && items.empty())
            return ast_builder_node(c, AST_NODE_RETURN, v, value, 0, 0, 0, 0);
        /* [stmt.return]/3: "The copy-initialization of the result of the
           call is sequenced before the destruction of temporaries at the
           end of the full-expression established by the operand of the
           return statement, which, in turn, is sequenced before the
           destruction of local variables ([stmt.jump]) of the block
           enclosing the return statement." */
        uint32_t result = 0;
        if (value && AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(value)) !=
                         ast_builder_type_basic(AST_TYPE_VOID)) {
            ast_qualified_type t = type_of(value);
            uint32_t held = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t,
                                               AST_STORAGE_AUTO, 0, 1);
            items.append(ast_builder_node(c, AST_NODE_DECLARATION, v, held,
                                          value, 0, 0, 0));
            result =
                make(AST_NODE_VARIABLE, t, held, 0, 0, 0, AST_NODE_FLAG_LVALUE);
        } else if (value) {
            items.append(
                ast_builder_node(c, AST_NODE_EXPRESSION, v, value, 0, 0, 0, 0));
        }
        for (uint32_t a : after)
            items.append(a);
        destroy(0, items, n.at);
        items.append(
            ast_builder_node(c, AST_NODE_RETURN, v, result, 0, 0, 0, 0));
        return block(items.data(), static_cast<uint32_t>(items.size()));
    }
    default:
        return fail("a statement of this kind", n.at);
    }
}

/* ---- definitions ---------------------------------------------------- */

/* [abi:vtable-components]: the array of class k's virtual table: the
   offset to top, the RTTI pointer (null: -fno-rtti until S7), then a
   function pointer per slot */
ast_qualified_type c_lowering::vtable_type(entity_index k)
{
    table_spec t;
    t.k = k;
    t.layout = k;
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    return ast_builder_type_array(c, ast_builder_type_pointer(c, v),
                                  table_length(t), true);
}

/*
 * [abi:vague-vtable]: "If the class has a key function ..., the virtual
 * table is emitted in the object file containing the definition of the
 * key function"; else it is emitted where it is used, each copy weak so
 * that they link as one (as an inline function's, symbol: COMDAT is the
 * C code generator's to come).  A class without linkage has a local one.
 */
uint32_t c_lowering::vtable(entity_index k, token_index at)
{
    if (vtables[k])
        return vtables[k];
    const dynamic_class *d = analysis.dynamic_of(k);
    if (!d)
        return fail("the virtual table of a class that is not dynamic", at);
    owned_buffer name;
    if (!itanium_vtable_name(analysis, tokens.pp, k, name.get()))
        return fail("a virtual table whose symbol is not mangled yet", at);
    const entity &x = analysis.entity_table[k];
    bool here =
        !d->key_function || analysis.entity_table[d->key_function].defined;
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    if (!d->key_function)
        flags |= AST_SYMBOL_FLAG_LINKONCE | AST_SYMBOL_FLAG_WEAK;
    if (x.linkage_of_name == linkage::internal ||
        x.linkage_of_name == linkage::none) {
        storage = AST_STORAGE_STATIC;
        flags = AST_SYMBOL_FLAG_GLOBAL;
    }
    if (!here)
        storage = AST_STORAGE_EXTERN;
    ast_qualified_type array = vtable_type(k);
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(),
                                    name.size(), array, storage, flags, 0);
    vtables[k] = s;
    ast_builder_toplevel(c, s);
    if (here) {
        table_spec t;
        t.k = k;
        t.layout = k;
        if (!table_definition(t, s, at))
            return 0;
    }
    if (x.virtual_base_count && !vtt_define(k, s, storage, flags, at))
        return 0;
    return s;
}

uint32_t c_lowering::address_point(entity_index k, uint32_t group,
                                   token_index at)
{
    table_spec t;
    t.k = k;
    t.layout = k;
    uint32_t s = vtable(k, at);
    if (!s)
        return 0;
    return element_address(s, vtable_type(k), table_point(t, group));
}

/* ---- virtual bases ([class.mi]/4) ---------------------------------- */

uint64_t c_lowering::virtual_base_offset(entity_index layout,
                                         entity_index v) const
{
    return analysis.entity_table[analysis.virtual_base_of(layout, v)].offset;
}

int64_t c_lowering::group_location(const table_spec &t, uint32_t group) const
{
    const vtable_group &g =
        analysis.vtable_groups[analysis.dynamic_of(t.k)->first_group + group];
    if (!g.in_virtual_base)
        return static_cast<int64_t>(g.offset);
    return static_cast<int64_t>(
               virtual_base_offset(t.layout, g.in_virtual_base) +
               g.offset_in_virtual) -
           static_cast<int64_t>(t.base_offset);
}

/* [abi:vtable-ctor-entries]: "Some of the base class subobjects may not
   need construction virtual tables": [impl.clang] those of non-virtual
   bases without virtual bases */
bool c_lowering::group_included(const table_spec &t, uint32_t group) const
{
    if (!t.construction || !group)
        return true;
    const vtable_group &g =
        analysis.vtable_groups[analysis.dynamic_of(t.k)->first_group + group];
    return g.in_virtual_base ||
           analysis.entity_table[g.subobject_class].virtual_base_count;
}

/*
 * [abi:vtable-components]: a table's vbase offsets, one per virtual base
 * of the class whose primary table it is, "added to the derived class
 * object address (i.e. the address of its virtual table pointer) to get
 * the address of a virtual base class subobject", nearest the address
 * point first; then, in a virtual base's own table, its vcall offsets
 * ([abi:vtable-construction] category 3: one per virtual function of
 * the base's primary table and of its non-virtual bases' tables, the
 * first of a signature only), each the offset from the virtual base to
 * the subobject of the final overrider.  No class overrides a function
 * of a virtual base yet (dynamic_class_record), so that subobject is in
 * the virtual base.
 */
void c_lowering::group_offsets(const table_spec &t, uint32_t group,
                               owned_array<int64_t> &out) const
{
    const vtable_group &g =
        analysis.vtable_groups[analysis.dynamic_of(t.k)->first_group + group];
    int64_t at = group_location(t, group);
    const dynamic_class *sd = analysis.dynamic_of(g.subobject_class);
    for (uint32_t i = 0; i < sd->vbase_entry_count; i++) {
        entity_index v = analysis.vbase_entries[sd->first_vbase_entry + i];
        out.append(static_cast<int64_t>(virtual_base_offset(t.layout, v)) -
                   static_cast<int64_t>(t.base_offset) - at);
    }
    /* the primary table of a virtual base: in the layout's table, or the
       construction table of a virtual base */
    entity_index virtual_class = 0;
    if (g.virtual_primary)
        virtual_class = g.in_virtual_base;
    else if (!group && t.is_virtual)
        virtual_class = t.k;
    if (!virtual_class)
        return;
    const dynamic_class *vd = analysis.dynamic_of(virtual_class);
    owned_array<entity_index> seen;
    for (uint32_t h = 0; h < vd->group_count; h++) {
        const vtable_group &vg = analysis.vtable_groups[vd->first_group + h];
        if (vg.in_virtual_base)
            continue;
        for (uint32_t k = 0; k < vg.slot_count; k++) {
            entity_index f = analysis.vtable_slots[vg.first_slot + k];
            bool repeated = false;
            for (entity_index e : seen)
                repeated =
                    repeated || e == f || analysis.overrides_function(f, e);
            if (repeated)
                continue;
            seen.append(f);
            out.append(static_cast<int64_t>(vg.offset) +
                       analysis.vtable_adjustments[vg.first_slot + k]);
        }
    }
}

uint32_t c_lowering::table_length(const table_spec &t) const
{
    const dynamic_class *d = analysis.dynamic_of(t.k);
    uint32_t length = 0;
    owned_array<int64_t> offsets;
    for (uint32_t g = 0; g < d->group_count; g++) {
        if (!group_included(t, g))
            continue;
        offsets.clear();
        group_offsets(t, g, offsets);
        length += static_cast<uint32_t>(offsets.size()) + 2 +
                  analysis.vtable_groups[d->first_group + g].slot_count;
    }
    return length;
}

uint32_t c_lowering::table_point(const table_spec &t, uint32_t group) const
{
    const dynamic_class *d = analysis.dynamic_of(t.k);
    uint32_t point = 0;
    owned_array<int64_t> offsets;
    for (uint32_t g = 0; g < d->group_count; g++) {
        if (!group_included(t, g)) {
            if (g == group)
                return UINT32_MAX;
            continue;
        }
        offsets.clear();
        group_offsets(t, g, offsets);
        point += static_cast<uint32_t>(offsets.size()) + 2;
        if (g == group)
            return point;
        point += analysis.vtable_groups[d->first_group + g].slot_count;
    }
    return UINT32_MAX;
}

uint32_t c_lowering::element_address(uint32_t s, ast_qualified_type array,
                                     uint32_t index)
{
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    ast_qualified_type vpp = ast_builder_type_pointer(c, vp);
    uint32_t table =
        make(AST_NODE_VARIABLE, array, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    address_taken(table);
    uint32_t element =
        make(AST_NODE_INDEX, vp, cast(table, vpp, AST_CAST_ARRAY_DECAY),
             to_ptrdiff(ast_builder_integer(
                 c, ast_builder_type_basic(AST_TYPE_INT), index)),
             0, 0, AST_NODE_FLAG_LVALUE);
    return address_of(element, vpp);
}

/*
 * [abi:vtable-ctor-vtt]: "The elements of the VTT array for a class D are
 * in this order: Primary virtual pointer ... Secondary VTTs: for each
 * direct non-virtual proper base class B of D that requires a VTT, in
 * declaration order, a sub-VTT for B-in-D, structured like the main VTT
 * for B ... Secondary virtual pointers: for each base class X which (a)
 * has virtual bases or is reachable along a virtual path from D, and (b)
 * is not a non-virtual primary base ... in inheritance graph preorder ...
 * Virtual VTTs: For each proper virtual base classes in inheritance graph
 * preorder, construct a sub-VTT".  The tables a sub-VTT's entries point
 * into are construction virtual tables, as clang's VTTBuilder makes them.
 */
void c_lowering::vtt_of(entity_index k, vtt_layout &out) const
{
    out.k = k;
    out.tables.clear();
    out.entries.clear();
    out.subs.clear();
    vtt_tables(out, k, 0, false);
}

void c_lowering::vtt_tables(vtt_layout &out, entity_index b, uint64_t at,
                            bool is_virtual) const
{
    const entity &x = analysis.entity_table[b];
    if (!x.virtual_base_count)
        return;
    if (b != out.k) {
        sub_vtt sub;
        sub.of = b;
        sub.offset = at;
        sub.index = static_cast<uint32_t>(out.entries.size());
        out.subs.append(sub);
    }
    uint32_t table = static_cast<uint32_t>(out.tables.size());
    table_spec spec;
    spec.k = b;
    spec.layout = out.k;
    spec.base_offset = at;
    spec.construction = b != out.k;
    spec.is_virtual = is_virtual;
    out.tables.append(spec);
    vtt_pointer(out, b, at, table);
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &base =
            analysis.entity_table[analysis.class_bases[x.first_base + i]];
        if (!base.virtual_base)
            vtt_tables(out, record(base.type).entity, at + base.offset, false);
    }
    owned_array<entity_index> seen;
    vtt_secondary_pointers(out, b, at, false, table, seen);
    if (b == out.k) {
        seen.clear();
        vtt_virtual(out, b, seen);
    }
}

/* the entry for the subobject of class b at offset at: the address point
   of the group of the table whose subobject that is */
void c_lowering::vtt_pointer(vtt_layout &out, entity_index b, uint64_t at,
                             uint32_t table) const
{
    const table_spec &t = out.tables[table];
    const dynamic_class *d = analysis.dynamic_of(t.k);
    (void)b;
    for (uint32_t g = 0; g < d->group_count; g++)
        if (group_included(t, g) &&
            static_cast<int64_t>(t.base_offset) + group_location(t, g) ==
                static_cast<int64_t>(at)) {
            vtt_entry e;
            e.table = table;
            e.group = g;
            out.entries.append(e);
            return;
        }
}

void c_lowering::vtt_secondary_pointers(vtt_layout &out, entity_index b,
                                        uint64_t at, bool morally_virtual,
                                        uint32_t table,
                                        owned_array<entity_index> &seen) const
{
    const entity &x = analysis.entity_table[b];
    if (!x.virtual_base_count && !morally_virtual)
        return;
    const dynamic_class *d = analysis.dynamic_of(b);
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index specifier = analysis.class_bases[x.first_base + i];
        const entity &base = analysis.entity_table[specifier];
        entity_index of = record(base.type).entity;
        if (!analysis.entity_table[of].dynamic)
            continue;
        bool base_morally_virtual = morally_virtual, nonvirtual_primary = false;
        uint64_t base_at = 0;
        if (base.virtual_base) {
            bool repeated = false;
            for (entity_index s : seen)
                repeated = repeated || s == of;
            if (repeated)
                continue;
            seen.append(of);
            base_at = virtual_base_offset(out.k, of);
            base_morally_virtual = true;
        } else {
            base_at = at + base.offset;
            nonvirtual_primary = d && d->primary_base == specifier;
        }
        if (!nonvirtual_primary &&
            (analysis.entity_table[of].virtual_base_count ||
             base_morally_virtual))
            vtt_pointer(out, of, base_at, table);
        vtt_secondary_pointers(out, of, base_at, base_morally_virtual, table,
                               seen);
    }
}

void c_lowering::vtt_virtual(vtt_layout &out, entity_index b,
                             owned_array<entity_index> &seen) const
{
    const entity &x = analysis.entity_table[b];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &base =
            analysis.entity_table[analysis.class_bases[x.first_base + i]];
        entity_index of = record(base.type).entity;
        if (base.virtual_base) {
            bool repeated = false;
            for (entity_index s : seen)
                repeated = repeated || s == of;
            if (repeated)
                continue;
            seen.append(of);
            vtt_tables(out, of, virtual_base_offset(out.k, of), true);
        }
        if (analysis.entity_table[of].virtual_base_count)
            vtt_virtual(out, of, seen);
    }
}

uint32_t c_lowering::vtt_index_of_group(entity_index k, uint32_t group) const
{
    vtt_layout layout;
    vtt_of(k, layout);
    for (uint32_t i = 0; i < layout.entries.size(); i++)
        if (!layout.entries[i].table && layout.entries[i].group == group)
            return i;
    return UINT32_MAX;
}

uint32_t c_lowering::sub_vtt_index(entity_index k, entity_index b,
                                   uint64_t offset) const
{
    vtt_layout layout;
    vtt_of(k, layout);
    for (const sub_vtt &sub : layout.subs)
        if (sub.of == b && sub.offset == offset)
            return sub.index;
    return UINT32_MAX;
}

/*
 * The definition of a virtual table: per included group, its vcall and
 * vbase offsets (furthest from the address point first), the offset to
 * top, the RTTI pointer (null: -fno-rtti until S7), then a function
 * pointer per slot ([abi:vtable-components]).  A construction virtual
 * table's offsets to top are from k's subobject ([abi:vtable-ctor-entries]:
 * "The offset-to-top fields refer to B").
 */
bool c_lowering::table_definition(const table_spec &t, uint32_t s,
                                  token_index at)
{
    const dynamic_class *d = analysis.dynamic_of(t.k);
    owned_array<uint32_t> runs;
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    uint32_t start = 0;
    owned_array<int64_t> offsets;
    auto integer = [&](uint32_t index, int64_t value) {
        if (!value)
            return;
        uint32_t n = ast_builder_integer(c, ast_builder_type_ptrdiff(c),
                                         static_cast<uint64_t>(value));
        runs.append(index);
        runs.append(index);
        runs.append(cast(n, vp, AST_CAST_INTEGER_TO_POINTER));
    };
    for (uint32_t g = 0; g < d->group_count; g++) {
        if (!group_included(t, g))
            continue;
        const vtable_group &group = analysis.vtable_groups[d->first_group + g];
        offsets.clear();
        group_offsets(t, g, offsets);
        uint32_t count = static_cast<uint32_t>(offsets.size());
        for (uint32_t i = 0; i < count; i++)
            integer(start + count - 1 - i, offsets[i]);
        start += count;
        /* [abi:vtable-components]: the offset to top, "the displacement
           to the top of the object from the location within the object
           of the virtual table pointer that addresses this virtual
           table" */
        integer(start, -group_location(t, g));
        for (uint32_t i = 0; i < group.slot_count; i++) {
            uint32_t slot = group.first_slot + i;
            entity_index f = analysis.vtable_slots[slot];
            int64_t adjustment = analysis.vtable_adjustments[slot];
            uint32_t target = 0;
            ast_qualified_type ft = 0;
            /* [abi:vtable-components]: a virtual destructor's pair of
               entries, the complete object destructor (the variant its
               calls use: D2 for an inline one, as clang) and the deleting
               one */
            bool destructor =
                analysis.entity_table[f].special == special_member::destructor;
            if (destructor && i && analysis.vtable_slots[slot - 1] == f) {
                ft = entity_type(f, at);
                if (!deleting_destructor(f, at, false))
                    return false;
                target = deleting_symbols[f];
            } else if (destructor) {
                ft = entity_type(f, at);
                target = special_callee(f, at);
            } else if (analysis.entity_table[f].pure) {
                /* [abi:pure-virtual]: a pure virtual function's slot */
                ft = ast_builder_type_function(c, v, nullptr, 0, 0);
                if (!pure_virtual) {
                    pure_virtual = ast_builder_symbol(
                        c, AST_SYMBOL_FUNCTION, "__cxa_pure_virtual", 18, ft,
                        AST_STORAGE_NONE, AST_SYMBOL_FLAG_GLOBAL, 0);
                    ast_builder_toplevel(c, pure_virtual);
                }
                target = pure_virtual;
            } else if (adjustment) {
                target = thunk(f, adjustment, at);
                ft = entity_type(f, at);
            } else {
                target = symbol(f, at);
                ft = entity_type(f, at);
            }
            if (!target)
                return false;
            ast_builder_symbol_flags_add(c, target,
                                         AST_SYMBOL_FLAG_USED |
                                             AST_SYMBOL_FLAG_ADDRESS_TAKEN);
            uint32_t function =
                cast(make(AST_NODE_VARIABLE, ft, target, 0, 0, 0, 0),
                     ast_builder_type_pointer(c, ft), AST_CAST_FUNCTION_DECAY);
            runs.append(start + 2 + i);
            runs.append(start + 2 + i);
            runs.append(cast(function, vp, AST_CAST_POINTER_TO_POINTER));
        }
        start += 2 + group.slot_count;
    }
    ast_qualified_type array =
        ast_builder_type_array(c, vp, table_length(t), true);
    uint32_t init = make(
        AST_NODE_INITIALIZER, array, 0,
        ast_builder_list(c, runs.data(), static_cast<uint32_t>(runs.size())),
        static_cast<uint32_t>(runs.size() / 3), 1, 0);
    ast_builder_symbol_body(c, s, init);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

/*
 * [abi:vtable-ctor-layout]: "The construction virtual tables for a
 * complete object are emitted in the same object file as the virtual
 * table", and the VTT with them, with the virtual table's linkage
 * ([impl.clang]: _ZTT <type>, _ZTC <type> <offset> _ <base type>).
 */
uint32_t c_lowering::vtt(entity_index k, token_index at)
{
    if (!vtable(k, at))
        return 0;
    if (!vtts[k])
        return fail("a VTT not made with its virtual table", at);
    ast_builder_symbol_flags_add(c, vtts[k], AST_SYMBOL_FLAG_USED);
    return vtts[k];
}

uint32_t c_lowering::vtt_define(entity_index k, uint32_t table, int storage,
                                uint32_t flags, token_index at)
{
    owned_buffer name;
    if (!itanium_vtt_name(analysis, tokens.pp, k, name.get()))
        return fail("a VTT whose symbol is not mangled yet", at);
    vtt_layout layout;
    vtt_of(k, layout);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    ast_qualified_type array =
        ast_builder_type_array(c, vp, layout.entries.size(), true);
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(),
                                    name.size(), array, storage, flags, 0);
    vtts[k] = s;
    ast_builder_toplevel(c, s);
    if (storage == AST_STORAGE_EXTERN)
        return s;
    /* [impl.clang]: emitted with the virtual table, used or not */
    ast_builder_symbol_keep(c, s);
    /* the tables, then the entries */
    owned_array<uint32_t> symbols_of;
    owned_array<ast_qualified_type> types_of;
    for (const table_spec &t : layout.tables) {
        if (!t.construction) {
            symbols_of.append(table);
            types_of.append(vtable_type(k));
            continue;
        }
        name.clear();
        if (!itanium_construction_vtable_name(analysis, tokens.pp, k,
                                              t.base_offset, t.k, name.get()))
            return fail("a construction virtual table whose symbol is not "
                        "mangled yet",
                        at);
        ast_qualified_type ta =
            ast_builder_type_array(c, vp, table_length(t), true);
        uint32_t ts = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(),
                                         name.size(), ta, storage, flags, 0);
        ast_builder_toplevel(c, ts);
        if (!table_definition(t, ts, at))
            return 0;
        symbols_of.append(ts);
        types_of.append(ta);
    }
    owned_array<uint32_t> runs;
    for (uint32_t i = 0; i < layout.entries.size(); i++) {
        const vtt_entry &e = layout.entries[i];
        uint32_t point = table_point(layout.tables[e.table], e.group);
        runs.append(i);
        runs.append(i);
        runs.append(
            cast(element_address(symbols_of[e.table], types_of[e.table], point),
                 vp, AST_CAST_POINTER_TO_POINTER));
    }
    uint32_t init = make(
        AST_NODE_INITIALIZER, array, 0,
        ast_builder_list(c, runs.data(), static_cast<uint32_t>(runs.size())),
        static_cast<uint32_t>(runs.size() / 3), 1, 0);
    ast_builder_symbol_body(c, s, init);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return s;
}

uint32_t c_lowering::virtual_base_address(uint32_t p, uint32_t again,
                                          entity_index k, entity_index v,
                                          ast_qualified_type t)
{
    const dynamic_class *d = analysis.dynamic_of(k);
    uint32_t index = 0;
    while (index < d->vbase_entry_count &&
           analysis.vbase_entries[d->first_vbase_entry + index] != v)
        index++;
    if (index == d->vbase_entry_count)
        return fail("a virtual base without a vbase offset", 0);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = ast_builder_type_pointer(c, byte);
    ast_qualified_type ptrdiff = ast_builder_type_ptrdiff(c);
    /* the vbase offset: ((ptrdiff_t *)*(char **)p)[-3 - index] */
    if (!p || !again)
        return 0;
    uint32_t table = make(AST_NODE_DEREFERENCE, bytes,
                          cast(p, ast_builder_type_pointer(c, bytes),
                               AST_CAST_POINTER_TO_POINTER),
                          0, 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t offset =
        make(AST_NODE_INDEX, ptrdiff,
             cast(table, ast_builder_type_pointer(c, ptrdiff),
                  AST_CAST_POINTER_TO_POINTER),
             ast_builder_integer(c, ptrdiff, -3 - static_cast<int64_t>(index)),
             0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t at = make(AST_NODE_INDEX, byte,
                       cast(again, bytes, AST_CAST_POINTER_TO_POINTER), offset,
                       0, 0, AST_NODE_FLAG_LVALUE);
    return cast(make(AST_NODE_ADDRESS, bytes, at, 0, 0, 0, 0), t,
                AST_CAST_POINTER_TO_POINTER);
}

bool c_lowering::at_this(uint32_t n) const
{
    for (;;) {
        const semantic_node &x = node(n);
        if (x.kind == semantic_kind::entity)
            return structor_this && x.a == structor_this;
        if (x.kind == semantic_kind::address ||
            x.kind == semantic_kind::indirection ||
            (x.kind == semantic_kind::conversion &&
             x.op == static_cast<uint8_t>(conversion_kind::lvalue_to_rvalue)))
            n = x.a;
        else
            return false;
    }
}

entity_index c_lowering::base_subobject_of(uint32_t n) const
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::member &&
        analysis.entity_table[x.b].base_subobject && at_this(x.a))
        return x.b;
    if (x.kind != semantic_kind::indirection)
        return 0;
    const semantic_node &p = node(x.a);
    if (p.kind == semantic_kind::conversion &&
        p.op == static_cast<uint8_t>(conversion_kind::derived_to_base) &&
        at_this(p.a))
        return p.b;
    return 0;
}

/* [abi:vtable-ctor-vtt]: the base object constructor or destructor of a
   base with virtual bases takes its sub-VTT: in the base object variant
   the VTT this one was given plus the sub-VTT's index, in the complete
   object one an element of the class's own VTT */
uint32_t c_lowering::vtt_argument(entity_index b, token_index at)
{
    const entity &base = analysis.entity_table[b];
    entity_index of = record(base.type).entity;
    if (!structor_variant || !analysis.entity_table[of].virtual_base_count)
        return 0;
    /* [abi:vtable-ctor-vtt]: a virtual base's is one of the virtual VTTs,
       which only the complete object variants pass */
    uint64_t offset = base.offset;
    if (base.virtual_base)
        offset = virtual_base_offset(structor_class, of);
    else if (base.enclosing != structor_class)
        return 0;
    uint32_t index = sub_vtt_index(structor_class, of, offset);
    if (index == UINT32_MAX)
        return fail("a base without a sub-VTT", at);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vpp =
        ast_builder_type_pointer(c, ast_builder_type_pointer(c, v));
    if (structor_variant == 2) {
        uint32_t p = make(AST_NODE_VARIABLE, vpp, vtt_parameter, 0, 0, 0,
                          AST_NODE_FLAG_LVALUE);
        uint32_t element =
            make(AST_NODE_INDEX, ast_builder_type_pointer(c, v), p,
                 to_ptrdiff(ast_builder_integer(
                     c, ast_builder_type_basic(AST_TYPE_INT), index)),
                 0, 0, AST_NODE_FLAG_LVALUE);
        return address_of(element, vpp);
    }
    uint32_t s = vtt(structor_class, at);
    if (!s)
        return 0;
    vtt_layout layout;
    vtt_of(structor_class, layout);
    ast_qualified_type array = ast_builder_type_array(
        c, ast_builder_type_pointer(c, v), layout.entries.size(), true);
    return element_address(s, array, index);
}

ast_qualified_type c_lowering::structor_type(entity_index e, token_index at)
{
    const entity &x = analysis.entity_table[e];
    const struct type &f = record(x.type);
    owned_array<ast_qualified_type> parameters;
    parameters.append(ast_builder_type_pointer(
        c, type(analysis.entity_table[x.enclosing].type, at)));
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    parameters.append(
        ast_builder_type_pointer(c, ast_builder_type_pointer(c, v)));
    const qualified_type *p =
        analysis.type_table.parameters(x.type.unqualified());
    for (uint32_t i = 0; i < f.parameter_count; i++)
        parameters.append(parameter_type(p[i], at));
    return ast_builder_type_function(c, type(f.operand, at), parameters.data(),
                                     f.parameter_count + 2, f.variadic);
}

/*
 * [class.cdtor]/4, [abi:vtable-ctor]: the virtual table pointers of the
 * class n.b stored in the object at n.a.  In a base object constructor or
 * destructor of a class with virtual bases, a pointer the VTT has an
 * entry for (the primary one first, so that the vbase offsets the others
 * are found with are the construction table's) is stored from the VTT it
 * was given, and one in a virtual base is found through its vbase offset;
 * otherwise each is the address point of the class's own table at its
 * subobject's offset.
 */
uint32_t c_lowering::vtable_pointer_stores(const semantic_node &n)
{
    entity_index k = n.b;
    const dynamic_class *d = analysis.dynamic_of(k);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    ast_qualified_type vpp = ast_builder_type_pointer(c, vp);
    ast_qualified_type vppp = ast_builder_type_pointer(c, vpp);
    bool from_vtt = structor_variant == 2 && k == structor_class &&
                    analysis.entity_table[k].virtual_base_count;
    vtt_layout layout;
    if (from_vtt)
        vtt_of(k, layout);
    uint32_t stores = 0;
    for (uint32_t g = 0; g < d->group_count; g++) {
        const vtable_group &group = analysis.vtable_groups[d->first_group + g];
        uint32_t point = 0, index = UINT32_MAX;
        for (uint32_t i = 0; from_vtt && i < layout.entries.size(); i++)
            if (!layout.entries[i].table && layout.entries[i].group == g &&
                index == UINT32_MAX)
                index = i;
        if (index != UINT32_MAX) {
            uint32_t p = make(AST_NODE_VARIABLE, vpp, vtt_parameter, 0, 0, 0,
                              AST_NODE_FLAG_LVALUE);
            point = make(AST_NODE_INDEX, vp, p,
                         to_ptrdiff(ast_builder_integer(
                             c, ast_builder_type_basic(AST_TYPE_INT), index)),
                         0, 0, AST_NODE_FLAG_LVALUE);
            point = cast(point, vpp, AST_CAST_POINTER_TO_POINTER);
        } else {
            point = address_point(k, g, n.at);
        }
        uint32_t where = 0;
        if (from_vtt && group.in_virtual_base) {
            where = virtual_base_address(expression(n.a), expression(n.a), k,
                                         group.in_virtual_base, vppp);
            if (where && group.offset_in_virtual)
                where = offset_pointer(
                    where, static_cast<int64_t>(group.offset_in_virtual), vppp);
        } else {
            uint32_t self = expression(n.a);
            if (!self)
                return 0;
            where = cast(self, vppp, AST_CAST_POINTER_TO_POINTER);
            if (group.offset)
                where = offset_pointer(self, static_cast<int64_t>(group.offset),
                                       vppp);
        }
        if (!where || !point)
            return 0;
        uint32_t slot = make(AST_NODE_DEREFERENCE, vpp, where, 0, 0, 0,
                             AST_NODE_FLAG_LVALUE);
        uint32_t store = make(AST_NODE_ASSIGN, vpp, slot, point, 0, 0, 0);
        if (stores)
            store = make(AST_NODE_COMMA, vpp, stores, store, 0, 0, 0);
        stores = store;
    }
    return stores;
}

uint32_t c_lowering::offset_pointer(uint32_t p, int64_t offset,
                                    ast_qualified_type t)
{
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = ast_builder_type_pointer(c, byte);
    uint32_t by = ast_builder_integer(c, ast_builder_type_ptrdiff(c),
                                      static_cast<uint64_t>(offset));
    uint32_t at =
        make(AST_NODE_INDEX, byte, cast(p, bytes, AST_CAST_POINTER_TO_POINTER),
             by, 0, 0, AST_NODE_FLAG_LVALUE);
    return cast(make(AST_NODE_ADDRESS, bytes, at, 0, 0, 0, 0), t,
                AST_CAST_POINTER_TO_POINTER);
}

/*
 * [abi:mangling-special-thunks]: a non-virtual thunk of f: "adjusts the this pointer by a
 * constant offset" and calls f, named _ZThn <offset> _ <f's encoding>
 * ([abi:mangling-special-thunks]).  It is defined where f is (with f's
 * linkage), declared elsewhere.
 */
uint32_t c_lowering::thunk(entity_index f, int64_t adjustment, token_index at)
{
    for (const thunk_record &t : thunks)
        if (t.f == f && t.adjustment == adjustment)
            return t.symbol;
    owned_buffer function_name;
    if (!itanium_symbol_name(analysis, tokens.pp, f, function_name.get()))
        return fail("a thunk whose function's symbol is not mangled yet", at);
    owned_buffer name;
    vbuffer_format_append(name.get(), "_ZThn%llu_",
                          static_cast<unsigned long long>(-adjustment));
    vbuffer_append(name.get(), function_name.data() + 2,
                   function_name.size() - 2);
    uint32_t target = symbol(f, at);
    if (!target)
        return 0;
    /* f's linkage, as symbol() gives it */
    const entity &x = analysis.entity_table[f];
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    if (x.inline_)
        flags |= AST_SYMBOL_FLAG_LINKONCE | AST_SYMBOL_FLAG_WEAK;
    entity_index local = local_class_function(f);
    if (x.linkage_of_name == linkage::internal ||
        (x.defined && without_linkage(x.type)) ||
        (local && !analysis.entity_table[local].inline_)) {
        storage = AST_STORAGE_STATIC;
        flags = AST_SYMBOL_FLAG_GLOBAL;
    }
    uint32_t s =
        ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name.data(), name.size(),
                           entity_type(f, at), storage, flags, 0);
    ast_builder_toplevel(c, s);
    thunk_record r;
    r.f = f;
    r.adjustment = adjustment;
    r.symbol = s;
    thunks.append(r);
    return s;
}

/* where virtual function f is defined: its thunks, one per adjustment
   its class's virtual table group has for it, each
   return f((C *)((char *)this + adjustment), arguments...) */
bool c_lowering::thunk_definitions(entity_index f, token_index at)
{
    const entity &x = analysis.entity_table[f];
    const dynamic_class *d = analysis.dynamic_of(x.enclosing);
    if (!d)
        return true;
    for (uint32_t g = 1; g < d->group_count; g++) {
        const vtable_group &group = analysis.vtable_groups[d->first_group + g];
        for (uint32_t i = 0; i < group.slot_count; i++) {
            uint32_t slot = group.first_slot + i;
            int64_t adjustment = analysis.vtable_adjustments[slot];
            if (analysis.vtable_slots[slot] != f || !adjustment)
                continue;
            uint32_t s = thunk(f, adjustment, at);
            if (!s)
                return false;
            bool done = false;
            for (thunk_record &t : thunks)
                if (t.symbol == s) {
                    done = t.defined;
                    t.defined = true;
                }
            if (done)
                continue;
            const struct type &ft = record(x.type);
            if (ft.variadic || passed_indirectly(ft.operand))
                return fail("a thunk of a variadic function or one returning "
                            "a class by address",
                            at);
            ast_qualified_type function = entity_type(f, at);
            const qualified_type *types =
                analysis.type_table.parameters(x.type.unqualified());
            owned_array<ast_qualified_type> parameter_types;
            parameter_types.append(ast_builder_type_pointer(
                c,
                type(qualified_type::make(
                         analysis.entity_table[x.enclosing].type.unqualified(),
                         x.member_cv),
                     at)));
            for (uint32_t k = 0; k < ft.parameter_count; k++)
                parameter_types.append(parameter_type(types[k], at));
            uint32_t count = ft.parameter_count + 1;
            owned_array<uint32_t> forwarded, arguments;
            for (uint32_t k = 0; k < count; k++) {
                ast_qualified_type t = parameter_types[k];
                uint32_t p = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0,
                                                t, AST_STORAGE_AUTO, 0, 1);
                ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
                forwarded.append(p);
                uint32_t value = make(AST_NODE_VARIABLE, t, p, 0, 0, 0,
                                      AST_NODE_FLAG_LVALUE);
                if (!k)
                    value = offset_pointer(value, adjustment, t);
                arguments.append(value);
            }
            uint32_t target = symbol(f, at);
            ast_builder_symbol_flags_add(c, target, AST_SYMBOL_FLAG_USED);
            uint32_t callee = cast(
                make(AST_NODE_VARIABLE, function, target, 0, 0, 0, 0),
                ast_builder_type_pointer(c, function), AST_CAST_FUNCTION_DECAY);
            ast_qualified_type result = type(ft.operand, at);
            uint32_t call =
                make(AST_NODE_CALL, result, callee,
                     ast_builder_list(c, arguments.data(), count), count, 0, 0);
            ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
            uint32_t statement_node = 0;
            if (record(ft.operand).kind == type_kind::fundamental &&
                AST_QUALIFIED_TYPE_UNQUALIFIED(result) == v)
                statement_node = ast_builder_node(c, AST_NODE_EXPRESSION, v,
                                                  call, 0, 0, 0, 0);
            else
                statement_node =
                    ast_builder_node(c, AST_NODE_RETURN, v, call, 0, 0, 0, 0);
            uint32_t body = block(&statement_node, 1);
            struct node &b = ast_builder_ast(c)->nodes[body];
            b.a = ast_builder_list(c, forwarded.data(), count);
            b.aux = static_cast<uint16_t>(count);
            ast_builder_symbol_body(c, s, body);
            ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
        }
    }
    return true;
}

/*
 * [abi:mangling-special-ctor-dtor] D0, the deleting destructor of virtual
 * destructor e: its symbol, and
 * when define its definition (where e is defined): D1(this), then the
 * deallocation function of [class.dtor]/12 given this
 * (and the class's size when it takes one, [expr.delete]/11).
 */
bool c_lowering::deleting_destructor(entity_index e, token_index at,
                                     bool define)
{
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, at, 0);
    if (!s)
        return false;
    if (!define)
        return true;
    entity_index k = x.enclosing;
    const dynamic_class *d = analysis.dynamic_of(k);
    if (!d || !d->deallocation) {
        fail("a deleting destructor without a deallocation function", at);
        return false;
    }
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type self =
        ast_builder_type_pointer(c, type(analysis.entity_table[k].type, at));
    uint32_t p = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, self,
                                    AST_STORAGE_AUTO, 0, 1);
    ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
    auto self_value = [&] {
        return make(AST_NODE_VARIABLE, self, p, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    };
    uint32_t statements[2];
    statements[0] = special_call(e, self_value(), nullptr, 0, at);
    if (!statements[0])
        return false;
    statements[0] =
        ast_builder_node(c, AST_NODE_EXPRESSION, v, statements[0], 0, 0, 0, 0);
    entity_index f = d->deallocation;
    uint32_t target = symbol(f, at);
    if (!target)
        return false;
    ast_builder_symbol_flags_add(c, target, AST_SYMBOL_FLAG_USED);
    ast_qualified_type function = entity_type(f, at);
    const qualified_type *types = analysis.type_table.parameters(
        analysis.entity_table[f].type.unqualified());
    uint32_t arguments[2];
    uint32_t count = 1;
    arguments[0] = cast(self_value(), parameter_type(types[0], at),
                        AST_CAST_POINTER_TO_POINTER);
    if (d->deallocation_sized) {
        arguments[1] = ast_builder_integer(
            c, AST_QUALIFIED_TYPE_UNQUALIFIED(parameter_type(types[1], at)),
            static_cast<int64_t>(analysis.entity_table[k].size));
        count = 2;
    }
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, function, target, 0, 0, 0, 0),
             ast_builder_type_pointer(c, function), AST_CAST_FUNCTION_DECAY);
    uint32_t call = make(AST_NODE_CALL, v, callee,
                         ast_builder_list(c, arguments, count), count, 0, 0);
    statements[1] =
        ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0);
    uint32_t body = block(statements, 2);
    struct node &b = ast_builder_ast(c)->nodes[body];
    b.a = ast_builder_list(c, &p, 1);
    b.aux = 1;
    ast_builder_symbol_body(c, s, body);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

/* the final overrider of virtual function f for the object at the address
   object: (F *)(*(void ***)object)[slot] */
uint32_t c_lowering::virtual_function(entity_index f, uint32_t object,
                                      token_index at, uint32_t next)
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    ast_qualified_type vpp = ast_builder_type_pointer(c, vp);
    uint32_t table = make(AST_NODE_DEREFERENCE, vpp,
                          cast(object, ast_builder_type_pointer(c, vpp),
                               AST_CAST_POINTER_TO_POINTER),
                          0, 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t slot = make(AST_NODE_INDEX, vp, table,
                         to_ptrdiff(ast_builder_integer(
                             c, ast_builder_type_basic(AST_TYPE_INT),
                             analysis.entity_table[f].vtable_slot + next)),
                         0, 0, AST_NODE_FLAG_LVALUE);
    ast_qualified_type ft = entity_type(f, at);
    return cast(slot, ast_builder_type_pointer(c, ft),
                AST_CAST_POINTER_TO_POINTER);
}

/* whether evaluating the lowered expression twice is evaluating it once:
   variables, their addresses, members, casts and reads through them */
bool c_lowering::side_effect_free(uint32_t lowered) const
{
    const struct node &n = ast_builder_ast(c)->nodes[lowered];
    switch (n.op) {
    case AST_NODE_VARIABLE:
    case AST_NODE_NUMBER:
        return true;
    case AST_NODE_INDEX:
    case AST_NODE_BINARY:
        return side_effect_free(n.a) && side_effect_free(n.b);
    case AST_NODE_ADDRESS:
    case AST_NODE_DEREFERENCE:
    case AST_NODE_CAST:
        return side_effect_free(n.a);
    case AST_NODE_MEMBER:
        return side_effect_free(n.a);
    default:
        return false;
    }
}

/*
 * The complete object variant of a constructor or destructor of a class
 * with virtual bases.  A constructor's is its body with the virtual bases'
 * initialization; [impl.clang] a destructor's calls the base object
 * destructor with the class's own VTT, then destroys the virtual bases.
 */
bool c_lowering::complete_variant(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &body = node(x.definition);
    uint32_t complete = symbol(e, body.at, 1);
    if (!complete)
        return false;
    if (x.special != special_member::destructor)
        return definition_body(e, complete, 1);
    const uint32_t *items = analysis.semantics.items(body.first);
    uint32_t self = symbol(items[0], body.at);
    if (!self)
        return false;
    ast_builder_symbol_flags_add(c, self, AST_SYMBOL_FLAG_PARAMETER);
    entity_index k = x.enclosing;
    uint32_t table = vtt(k, body.at);
    if (!table)
        return false;
    vtt_layout layout;
    vtt_of(k, layout);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type array = ast_builder_type_array(
        c, ast_builder_type_pointer(c, v), layout.entries.size(), true);
    ast_qualified_type pointer = ast_builder_type_pointer(
        c, type(analysis.entity_table[k].type, body.at));
    uint32_t call = special_call(
        e,
        make(AST_NODE_VARIABLE, pointer, self, 0, 0, 0, AST_NODE_FLAG_LVALUE),
        nullptr, 0, body.at, element_address(table, array, 0));
    if (!call)
        return false;
    labels.clear();
    live.clear();
    in_function = true;
    structor_variant = 3;
    structor_class = k;
    structor_this = items[0];
    uint32_t statements[2] = {
        ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0),
        statement(body.c)};
    in_function = false;
    structor_variant = 0;
    structor_class = 0;
    structor_this = 0;
    if (failure.what)
        return false;
    uint32_t block_node = block(statements, 2);
    struct node &b = ast_builder_ast(c)->nodes[block_node];
    b.a = ast_builder_list(c, &self, 1);
    b.aux = 1;
    ast_builder_symbol_body(c, complete, block_node);
    ast_builder_symbol_flags_add(c, complete, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::complete_variants()
{
    for (bool again = true; again;) {
        again = false;
        for (uint32_t i = 0; i < inline_structors.size(); i++) {
            entity_index e = inline_structors[i];
            if (!e || !complete_symbols[e])
                continue;
            inline_structors[i] = 0;
            again = true;
            if (!complete_variant(e))
                return false;
        }
    }
    return true;
}

bool c_lowering::with_virtual_bases(entity_index e) const
{
    const entity &x = analysis.entity_table[e];
    return x.special != special_member::none &&
           analysis.entity_table[x.enclosing].virtual_base_count;
}

bool c_lowering::definition_body(entity_index e, uint32_t s, int variant)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &body = node(x.definition);
    labels.clear();
    in_function = true;
    /* every parameter has a symbol, an unnamed one too */
    owned_array<uint32_t> parameters;
    const struct type &f = record(x.type);
    const qualified_type *types =
        analysis.type_table.parameters(x.type.unqualified());
    const uint32_t *items = analysis.semantics.items(body.first);
    /* a member function's this comes first */
    uint32_t implicit = 0;
    if (body.count > f.parameter_count)
        implicit = body.count - f.parameter_count;
    for (uint32_t i = 0; i < body.count; i++) {
        uint32_t p = 0;
        if (items[i])
            p = symbol(items[i], body.at);
        else if (i >= implicit)
            p = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0,
                                   parameter_type(types[i - implicit], body.at),
                                   AST_STORAGE_AUTO, 0, 1);
        if (!p)
            return fail("a parameter", body.at);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        parameters.append(p);
    }
    structor_variant = variant;
    structor_class = 0;
    structor_this = 0;
    vtt_parameter = 0;
    if (variant) {
        structor_class = x.enclosing;
        structor_this = items[0];
    }
    if (variant == 2) {
        /* the VTT parameter after this */
        ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
        vtt_parameter = ast_builder_symbol(
            c, AST_SYMBOL_VARIABLE, "", 0,
            ast_builder_type_pointer(c, ast_builder_type_pointer(c, v)),
            AST_STORAGE_AUTO, 0, 1);
        ast_builder_symbol_flags_add(c, vtt_parameter,
                                     AST_SYMBOL_FLAG_PARAMETER);
        owned_array<uint32_t> with_vtt;
        for (uint32_t i = 0; i < parameters.size(); i++) {
            with_vtt.append(parameters[i]);
            if (!i)
                with_vtt.append(vtt_parameter);
        }
        parameters.clear();
        for (uint32_t p : with_vtt)
            parameters.append(p);
    }
    live.clear();
    named_return = 0;
    if (passed_indirectly(f.operand)) {
        bool mixed = false;
        entity_index found = 0;
        find_named_return(body.a, found, mixed);
        if (!mixed)
            named_return = found;
    }
    if (body.c) {
        scope_record r;
        r.kind = scope_record::subobjects;
        r.symbol = body.c;
        live.append(r);
    }
    uint32_t prefix = 0;
    if (body.b)
        prefix = statement(body.b);
    uint32_t block_node = scoped_block(node(body.a), prefix);
    if (body.c) {
        /* [class.dtor]/13: after the body, the members and bases */
        uint32_t items[2] = {block_node, statement(body.c)};
        block_node = block(items, 2);
    }
    in_function = false;
    structor_variant = 0;
    structor_class = 0;
    structor_this = 0;
    vtt_parameter = 0;
    if (failure.what)
        return false;
    struct node &b = ast_builder_ast(c)->nodes[block_node];
    uint32_t count = static_cast<uint32_t>(parameters.size());
    b.a = ast_builder_list(c, parameters.data(), count);
    b.aux = static_cast<uint16_t>(count);
    ast_builder_symbol_body(c, s, block_node);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::function_definition(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &body = node(x.definition);
    if (with_virtual_bases(e)) {
        /* [abi:mangling-special-ctor-dtor]: the base object variant (the
           VTT after this, no virtual base), then the complete object one,
           each from the body; the second lowering makes its own symbols
           of the automatic variables */
        uint32_t base = symbol(e, body.at, 2);
        if (!base)
            return false;
        owned_array<entity_index> before;
        for (entity_index i = 0; i < symbols.size(); i++)
            if (!symbols[i])
                before.append(i);
        if (!definition_body(e, base, 2))
            return false;
        for (entity_index i : before)
            if (analysis.entity_table[i].kind == entity_kind::variable &&
                analysis.entity_table[i].automatic)
                symbols[i] = 0;
        if (x.inline_ && !complete_symbols[e]) {
            /* [abi:vague-inline]: made where called (complete_variants) */
            inline_structors.append(e);
        } else if (!complete_variant(e)) {
            return false;
        }
        if (x.virtual_ && !thunk_definitions(e, body.at))
            return false;
        return !(x.virtual_ && x.special == special_member::destructor &&
                 !deleting_destructor(e, body.at, true));
    }
    uint32_t s = symbol(e, body.at);
    if (!s || !definition_body(e, s, 0))
        return false;
    const struct type &f = record(x.type);
    const qualified_type *types =
        analysis.type_table.parameters(x.type.unqualified());
    if (x.virtual_ && !thunk_definitions(e, body.at))
        return false;
    if (x.virtual_ && x.special == special_member::destructor &&
        !deleting_destructor(e, body.at, true))
        return false;
    if (x.special == special_member::none || x.inline_)
        return true;
    /* [abi:mangling-special-ctor-dtor]: the complete object variant, which
       for a class without virtual bases does what the base object one does
       (clang makes it an alias): a call of it */
    uint32_t complete = symbol(e, body.at, 1);
    if (!complete)
        return false;
    owned_array<uint32_t> forwarded, arguments;
    owned_array<ast_qualified_type> parameter_types;
    parameter_types.append(ast_builder_type_pointer(
        c, type(analysis.entity_table[x.enclosing].type, body.at)));
    for (uint32_t i = 0; i < f.parameter_count; i++)
        parameter_types.append(parameter_type(types[i], body.at));
    for (uint32_t i = 0; i < body.count; i++) {
        ast_qualified_type t = parameter_types[i];
        uint32_t p = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t,
                                        AST_STORAGE_AUTO, 0, 1);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        forwarded.append(p);
        arguments.append(
            make(AST_NODE_VARIABLE, t, p, 0, 0, 0, AST_NODE_FLAG_LVALUE));
    }
    ast_qualified_type function = entity_type(e, body.at);
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, function, s, 0, 0, 0, 0),
             ast_builder_type_pointer(c, function), AST_CAST_FUNCTION_DECAY);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    uint32_t call = make(AST_NODE_CALL, v, callee,
                         ast_builder_list(c, arguments.data(), body.count),
                         body.count, 0, 0);
    uint32_t statement_node =
        ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0);
    uint32_t thunk = block(&statement_node, 1);
    struct node &t = ast_builder_ast(c)->nodes[thunk];
    t.a = ast_builder_list(c, forwarded.data(), body.count);
    t.aux = static_cast<uint16_t>(body.count);
    ast_builder_symbol_body(c, complete, thunk);
    ast_builder_symbol_flags_add(c, complete, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::variable_definition(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, 0);
    if (!s)
        return false;
    if (x.definition) {
        /* [basic.start.static]/2: constant initialization; anything else is
           dynamic initialization, which needs code before main */
        if (!constant_initializer(x.definition) &&
            !needs_run_time(x.definition))
            return fail("a constant whose value analysis does not know",
                        node(x.definition).at);
        qualified_type object;
        entity_index destructor = element_destructor(x.type, object);
        if (destructor && !(object == x.type))
            /* [basic.start.term]: clang's __cxx_global_array_dtor */
            return fail("a static array with a destructor",
                        node(x.definition).at);
        bool constant = constant_initializer(x.definition);
        if (x.inline_ && (!constant || destructor))
            /* [basic.start.dynamic]/1: partially-ordered, under a guard in
               a COMDAT group ([abi:guards]) */
            return fail("an inline variable with dynamic initialization or a "
                        "destructor",
                        node(x.definition).at);
        if (!constant) {
            /* [basic.start.static]/2: zero-initialized, then initialized
               by dynamic_initialization */
            dynamic.append(e);
            destroy_only.append(0);
            ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
            return true;
        }
        uint32_t init = expression(x.definition);
        if (!init)
            return false;
        ast_builder_symbol_body(c, s, init);
        if (destructor) {
            /* [basic.start.term]/1: constant-initialized, its destructor
               registered at start-up */
            dynamic.append(e);
            destroy_only.append(1);
        }
    } else {
        qualified_type object;
        if (element_destructor(x.type, object))
            /* [basic.start.term] */
            return fail("a static object with a destructor and no "
                        "initializer",
                        0);
    }
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

/* a function void name(void) of internal linkage, named as written (its
   body to come) */
uint32_t c_lowering::local_function(const char *name, size_t length)
{
    ast_qualified_type f = ast_builder_type_function(
        c, ast_builder_type_basic(AST_TYPE_VOID), nullptr, 0, 0);
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name, length, f,
                                    AST_STORAGE_STATIC, 0, 0);
    owned_buffer written;
    if (target == PP_DARWIN)
        vbuffer_string_append(written.get(), "_");
    vbuffer_append(written.get(), name, length);
    ast_builder_symbol_assembler_name(c, s, written.data(), written.size());
    ast_builder_toplevel(c, s);
    return s;
}

/* the initialization of variable e of static storage duration by
   initializer init, as an expression: a constructor's call or an
   assignment */
uint32_t c_lowering::initialization(entity_index e, uint32_t init,
                                    token_index at)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &n = node(init);
    uint32_t s = symbol(e, at);
    if (!s)
        return 0;
    ast_qualified_type t = type(x.type, at);
    uint32_t variable =
        make(AST_NODE_VARIABLE, t, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
    if (n.kind == semantic_kind::construct)
        return construct_at(n, variable, 0, at);
    if (is_reference(x.type)) {
        uint32_t value = expression(init);
        if (!value)
            return 0;
        return make(AST_NODE_ASSIGN, t, variable, value, 0, 0, 0);
    }
    uint32_t value = initializer(init);
    if (!value)
        return 0;
    return make(AST_NODE_ASSIGN, AST_QUALIFIED_TYPE_UNQUALIFIED(t), variable,
                value, 0, 0, 0);
}

/* a call of the run time's function name (declared on first use, in
   slot) */
uint32_t c_lowering::runtime_call(uint32_t &slot, const char *name,
                                  ast_qualified_type result,
                                  const ast_qualified_type *parameters,
                                  const uint32_t *arguments, uint32_t count)
{
    ast_qualified_type ft =
        ast_builder_type_function(c, result, parameters, count, false);
    if (!slot) {
        slot =
            ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name, strlen(name), ft,
                               AST_STORAGE_NONE, AST_SYMBOL_FLAG_GLOBAL, 0);
        ast_builder_toplevel(c, slot);
    }
    ast_builder_symbol_flags_add(c, slot, AST_SYMBOL_FLAG_USED);
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, ft, slot, 0, 0, 0, 0),
             ast_builder_type_pointer(c, ft), AST_CAST_FUNCTION_DECAY);
    owned_array<uint32_t> all;
    for (uint32_t i = 0; i < count; i++) {
        if (!arguments[i])
            return 0;
        ast_qualified_type a = type_of(arguments[i]);
        if (a == parameters[i])
            all.append(arguments[i]);
        else
            all.append(
                cast(arguments[i], parameters[i], AST_CAST_POINTER_TO_POINTER));
    }
    return make(AST_NODE_CALL, result, callee,
                ast_builder_list(c, all.data(), count), count, 0, 0);
}

/* [basic.start.term]/3, [abi:dso-dtor]: "__cxa_atexit(f, p, d) ... f(p)
   is called when the shared object d is unloaded": the complete object
   destructor of the object at address object, registered for
   __dso_handle's module, as clang does */
uint32_t c_lowering::destructor_registration(entity_index destructor,
                                             uint32_t object, token_index at)
{
    uint32_t d = special_callee(destructor, at);
    if (!d || !object)
        return 0;
    ast_builder_symbol_flags_add(c, d, AST_SYMBOL_FLAG_ADDRESS_TAKEN);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type pointer = ast_builder_type_pointer(c, v);
    ast_qualified_type dt = ast_builder_type_function(c, v, &pointer, 1, false);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    if (!dso_handle) {
        dso_handle =
            ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "__dso_handle", 12, byte,
                               AST_STORAGE_EXTERN, AST_SYMBOL_FLAG_GLOBAL, 0);
        ast_builder_toplevel(c, dso_handle);
    }
    ast_builder_symbol_flags_add(c, dso_handle, AST_SYMBOL_FLAG_USED);
    ast_qualified_type f = entity_type(destructor, at);
    uint32_t function =
        cast(make(AST_NODE_VARIABLE, f, d, 0, 0, 0, 0),
             ast_builder_type_pointer(c, f), AST_CAST_FUNCTION_DECAY);
    uint32_t handle = address_of(make(AST_NODE_VARIABLE, byte, dso_handle, 0, 0,
                                      0, AST_NODE_FLAG_LVALUE),
                                 ast_builder_type_pointer(c, byte));
    ast_qualified_type parameters[3] = {ast_builder_type_pointer(c, dt),
                                        pointer, pointer};
    uint32_t arguments[3] = {function, object, handle};
    return runtime_call(cxa_atexit, "__cxa_atexit",
                        ast_builder_type_basic(AST_TYPE_INT), parameters,
                        arguments, 3);
}

/*
 * [stmt.dcl]/4: "Dynamic initialization of a block-scope variable with
 * static storage duration ... is performed the first time control passes
 * through its declaration ... If control enters the declaration
 * concurrently while the variable is being initialized, the concurrent
 * execution shall wait for completion of the initialization."
 * [abi:guards]: a 64-bit guard variable _ZGV<the variable's mangled name
 * without _Z>, its first byte read with acquire semantics, then
 * __cxa_guard_acquire, the initialization, the destructor's registration
 * and __cxa_guard_release, as clang does at -O0.  A constant-initialized
 * object with a destructor is guarded for its registration alone.
 */
void c_lowering::guarded_initialization(const semantic_node &n,
                                        entity_index destructor,
                                        owned_array<uint32_t> &out)
{
    entity_index e = n.a;
    const entity &x = analysis.entity_table[e];
    qualified_type object = x.type;
    if (destructor && record(object).kind != type_kind::class_type) {
        fail("a static array with a destructor", n.at);
        return;
    }
    entity_index function = enclosing_function(e);
    if (!function || analysis.entity_table[function].inline_) {
        /* [abi:guards]: the guard and the variable are COMDAT with the
           inline function, which C's code generator does not make yet */
        fail("a guarded local static in an inline function", n.at);
        return;
    }
    bool constant = !n.b || constant_initializer(n.b);
    if (!constant && node(n.b).kind == semantic_kind::initializer_list) {
        fail("dynamic initialization of an aggregate", n.at);
        return;
    }
    uint32_t s = symbol(e, n.at);
    if (!s)
        return;
    uint32_t data = 0;
    if (n.b && constant) {
        data = initializer(n.b);
        if (!data)
            return;
    }
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    ast_builder_symbol_keep(c, s);
    if (data)
        ast_builder_symbol_body(c, s, data);
    owned_buffer name;
    if (!itanium_symbol_name(analysis, tokens.pp, e, name.get(), 0) ||
        name.size() < 2) {
        fail("an entity whose symbol is not mangled yet", n.at);
        return;
    }
    owned_buffer guard_name;
    vbuffer_string_append(guard_name.get(), "_ZGV");
    vbuffer_append(guard_name.get(), name.data() + 2, name.size() - 2);
    owned_buffer written;
    if (target == PP_DARWIN)
        vbuffer_string_append(written.get(), "_");
    vbuffer_append(written.get(), guard_name.data(), guard_name.size());
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type i64 =
        ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
    ast_qualified_type i64p = ast_builder_type_pointer(c, i64);
    ast_qualified_type int_type = ast_builder_type_basic(AST_TYPE_INT);
    uint32_t guard =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, guard_name.data(),
                           guard_name.size(), i64, AST_STORAGE_STATIC, 0, 1);
    ast_builder_symbol_assembler_name(c, guard, written.data(), written.size());
    ast_builder_symbol_flags_add(c, guard, AST_SYMBOL_FLAG_DEFINED);
    ast_builder_symbol_keep(c, guard);
    auto guard_address = [&]() {
        return address_of(
            make(AST_NODE_VARIABLE, i64, guard, 0, 0, 0, AST_NODE_FLAG_LVALUE),
            i64p);
    };
    /* the initialization's temporaries would be made before the guard:
       not yet */
    size_t first = temporaries.size(), first_destruction = destructions.size();
    owned_array<uint32_t> inner;
    if (!constant) {
        uint32_t done = initialization(e, n.b, n.at);
        if (!done)
            return;
        inner.append(
            ast_builder_node(c, AST_NODE_EXPRESSION, v, done, 0, 0, 0, 0));
    }
    if (temporaries.size() != first ||
        destructions.size() != first_destruction) {
        fail("temporaries in a guarded local static's initializer", n.at);
        return;
    }
    if (destructor) {
        ast_qualified_type t = type(x.type, n.at);
        uint32_t variable =
            make(AST_NODE_VARIABLE, t, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
        uint32_t registered = destructor_registration(
            destructor, address_of(variable, ast_builder_type_pointer(c, t)),
            n.at);
        if (!registered)
            return;
        inner.append(ast_builder_node(c, AST_NODE_EXPRESSION, v, registered, 0,
                                      0, 0, 0));
    }
    uint32_t argument = guard_address();
    uint32_t release = runtime_call(guard_release, "__cxa_guard_release", v,
                                    &i64p, &argument, 1);
    inner.append(
        ast_builder_node(c, AST_NODE_EXPRESSION, v, release, 0, 0, 0, 0));
    argument = guard_address();
    uint32_t acquire = runtime_call(guard_acquire, "__cxa_guard_acquire",
                                    int_type, &i64p, &argument, 1);
    uint32_t acquired = ast_builder_node(
        c, AST_NODE_IF, v,
        make(AST_NODE_BINARY, int_type, acquire,
             ast_builder_integer(c, int_type, 0), 0,
             LEX_TOKEN_EXCLAMATION_EQUAL, 0),
        block(inner.data(), static_cast<uint32_t>(inner.size())), 0, 0, 0);
    /* the guard's first byte, read with acquire semantics
       (__atomic_load_n(p, __ATOMIC_ACQUIRE)) */
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_UNSIGNED_CHAR);
    uint32_t loaded = ast_builder_atomic_load(
        c, byte,
        cast(guard_address(), ast_builder_type_pointer(c, byte),
             AST_CAST_POINTER_TO_POINTER),
        ast_builder_integer(c, int_type, 2));
    uint32_t test = make(
        AST_NODE_BINARY, int_type, cast(loaded, int_type, AST_CAST_INTEGER),
        ast_builder_integer(c, int_type, 0), 0, LEX_TOKEN_EQUAL_EQUAL, 0);
    out.append(ast_builder_node(c, AST_NODE_IF, v, test, acquired, 0, 0, 0));
}

/*
 * [basic.start.dynamic]/2: "Dynamic initialization of non-block variables
 * ... Within a single translation unit, the initialization of ... variables
 * with ordered initialization is sequenced before ... initialization of
 * the variables defined later in the translation unit."  [impl.clang]: one
 * internal function per variable, __cxx_global_var_init (then .1, .2,
 * ...), called in order by _GLOBAL__sub_I_<the source file's name>, which
 * .init_array runs before main ([basic.start.dynamic]/5 lets it).
 */
bool c_lowering::dynamic_initialization()
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    owned_array<uint32_t> calls;
    for (size_t i = 0; i < dynamic.size(); i++) {
        entity_index e = dynamic[i];
        const entity &x = analysis.entity_table[e];
        const semantic_node &init = node(x.definition);
        bool only_destroy = destroy_only[i];
        if (!only_destroy && init.kind == semantic_kind::initializer_list)
            /* an aggregate's elements, some not constant: each stored */
            return fail("dynamic initialization of an aggregate", init.at);
        qualified_type object = x.type;
        entity_index destructor = 0;
        if (record(object).kind == type_kind::class_type)
            destructor =
                analysis.entity_table[record(object).entity].destructor;
        owned_buffer name;
        vbuffer_string_append(name.get(), "__cxx_global_var_init");
        if (i)
            vbuffer_format_append(name.get(), ".%u", static_cast<unsigned>(i));
        uint32_t f = local_function(name.data(), name.size());
        in_function = true;
        labels.clear();
        live.clear();
        owned_array<uint32_t> statements;
        if (!only_destroy) {
            uint32_t done = initialization(e, x.definition, init.at);
            if (!done) {
                in_function = false;
                return false;
            }
            statements.append(
                ast_builder_node(c, AST_NODE_EXPRESSION, v, done, 0, 0, 0, 0));
        }
        if (destructor && !failure.what) {
            /* [basic.start.term]/3: registered with __cxa_atexit once
               constructed ([abi:dso-dtor]) */
            uint32_t s = symbol(e, init.at);
            ast_qualified_type t = type(x.type, init.at);
            uint32_t variable =
                make(AST_NODE_VARIABLE, t, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
            uint32_t registered = destructor_registration(
                destructor,
                address_of(variable, ast_builder_type_pointer(c, t)), init.at);
            if (registered)
                statements.append(ast_builder_node(c, AST_NODE_EXPRESSION, v,
                                                   registered, 0, 0, 0, 0));
        }
        in_function = false;
        if (failure.what || statements.empty())
            return false;
        ast_builder_symbol_body(
            c, f,
            block(statements.data(), static_cast<uint32_t>(statements.size())));
        ast_builder_symbol_flags_add(
            c, f, AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_USED);
        ast_qualified_type ft = ast_builder_type_function(c, v, nullptr, 0, 0);
        uint32_t callee =
            cast(make(AST_NODE_VARIABLE, ft, f, 0, 0, 0, 0),
                 ast_builder_type_pointer(c, ft), AST_CAST_FUNCTION_DECAY);
        uint32_t call = make(AST_NODE_CALL, v, callee, 0, 0, 0, 0);
        calls.append(
            ast_builder_node(c, AST_NODE_EXPRESSION, v, call, 0, 0, 0, 0));
    }
    owned_buffer name;
    vbuffer_string_append(name.get(), "_GLOBAL__sub_I_");
    const char *base = main_path;
    if (!base)
        base = "";
    for (const char *p = base; *p; p++)
        if (*p == '/')
            base = p + 1;
    /* [impl.clang]: the file's name with each character that cannot
       continue a pp-number ([lex.ppnumber]: a letter, a digit, _ or .)
       replaced by _ */
    for (const char *p = base; *p; p++) {
        char k = *p;
        if (!((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') ||
              (k >= '0' && k <= '9') || k == '_' || k == '.'))
            k = '_';
        vbuffer_append(name.get(), &k, 1);
    }
    uint32_t start = local_function(name.data(), name.size());
    ast_builder_symbol_body(
        c, start, block(calls.data(), static_cast<uint32_t>(calls.size())));
    ast_builder_symbol_flags_add(c, start, AST_SYMBOL_FLAG_DEFINED);
    ast_builder_symbol_constructor(c, start);
    return true;
}

bool c_lowering::translation_unit()
{
    for (const analysed_declaration &d : analysis.declarations)
        if (d.result != analysis_result::well_formed) {
            token_index at = analysis.syntax()[d.node].first_token;
            if (d.result == analysis_result::unsupported &&
                d.first_unsupported) {
                const unsupported_construct &u =
                    analysis.unsupported[d.first_unsupported];
                failure.subclause = u.subclause;
                at = analysis.syntax()[u.node].first_token;
            }
            if (d.result == analysis_result::not_parsed)
                fail("a declaration the parser kept as tokens", at);
            else
                fail("a declaration that is not well-formed", at);
            return false;
        }
    /* the namespace-scope functions and variables, in the order they are
       first declared */
    for (const declared_name &d : analysis.declared) {
        if (!d.entity)
            continue;
        const entity &x = analysis.entity_table[d.entity];
        if (x.kind != entity_kind::function && x.kind != entity_kind::variable)
            continue;
        if (x.automatic || enclosing_function(d.entity))
            continue;
        if (!symbol(d.entity, 0))
            return false;
    }
    owned_array<entity_index> done;
    for (const declared_name &d : analysis.declared) {
        if (!d.entity)
            continue;
        const entity &x = analysis.entity_table[d.entity];
        /* a deleted function has no code ([dcl.fct.def.delete]) */
        if (!x.defined || x.automatic || enclosing_function(d.entity) ||
            x.deleted)
            continue;
        /* [temp.explicit]/11: an explicit instantiation declaration's
           specialization is defined in another translation unit (clang
           emits none, not even an inline one) */
        if (analysis.explicitly_instantiated(d.entity) ==
            instantiation_kind::declaration)
            continue;
        bool again = false;
        for (entity_index s : done)
            if (s == d.entity)
                again = true;
        if (again)
            continue;
        done.append(d.entity);
        if (x.kind == entity_kind::function) {
            if (!function_definition(d.entity))
                return false;
        } else if (x.kind == entity_kind::variable) {
            if (!variable_definition(d.entity))
                return false;
        }
    }
    if (!dynamic.empty() && !dynamic_initialization())
        return false;
    /* [abi:vague-vtable]: the virtual table of each class whose key
       function this translation unit defines */
    for (entity_index k = 1; k < analysis.entity_table.size(); k++) {
        const dynamic_class *d = analysis.dynamic_of(k);
        if (d && d->key_function &&
            analysis.entity_table[d->key_function].defined && !vtable(k, 0))
            return false;
    }
    /* the member functions defined, in the order of their definitions */
    for (const semantic_analysis::definition_record &d : analysis.definitions) {
        const entity &x = analysis.entity_table[d.entity];
        if (x.kind != entity_kind::function || !x.enclosing ||
            analysis.entity_table[x.enclosing].kind != entity_kind::class_type)
            continue;
        if (analysis.explicitly_instantiated(d.entity) ==
            instantiation_kind::declaration)
            continue;
        if (!function_definition(d.entity))
            return false;
    }
    if (!complete_variants())
        return false;
    return !failure.what;
}

} // namespace

bool lower_translation_unit(const semantic_analysis &analysis,
                            token_stream &tokens, cc *c, int target,
                            lowering_failure &why, const char *main_path)
{
    c_lowering lowering(analysis, tokens, c, target, main_path);
    bool lowered = lowering.translation_unit();
    why = lowering.failure;
    return lowered;
}

} // namespace cplusplus
