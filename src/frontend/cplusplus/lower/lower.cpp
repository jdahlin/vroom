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

/* C nodes said once */
uint32_t c_variable(cc *c, uint32_t s, ast_qualified_type t)
{
    return ast_builder_node(c, AST_NODE_VARIABLE, t, s, 0, 0, 0,
                            AST_NODE_FLAG_LVALUE);
}

uint32_t c_statement(cc *c, uint32_t e)
{
    return ast_builder_node(c, AST_NODE_EXPRESSION,
                            ast_builder_type_basic(AST_TYPE_VOID), e, 0, 0, 0,
                            0);
}

uint32_t c_declare(cc *c, uint32_t s, uint32_t init = 0)
{
    return ast_builder_node(c, AST_NODE_DECLARATION,
                            ast_builder_type_basic(AST_TYPE_VOID), s, init, 0,
                            0, 0);
}

/* an unnamed automatic variable of type t */
uint32_t c_local(cc *c, ast_qualified_type t)
{
    return ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t,
                              AST_STORAGE_AUTO, 0, 1);
}

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
             guard_release = 0;
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
        ast_qualified_type type = 0;
        entity_index destructor = 0;
        /* an array's: its element type and bound (0: not an array) */
        ast_qualified_type element = 0;
        uint64_t bound = 0;
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
    };
    owned_array<scope_record> live;
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
       member e on its address */
    uint32_t each_element(uint32_t s, ast_qualified_type array,
                          ast_qualified_type element, uint64_t bound,
                          bool reverse, entity_index e, token_index at);

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
    uint32_t initialize_into(uint32_t s, qualified_type object_type,
                             ast_qualified_type t, uint32_t n);
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
    void destroyed_after(entity_index destructor, uint32_t temporary,
                         ast_qualified_type t, token_index at,
                         uint32_t flag = 0);
    /* [class.temporary]/6: the prvalue whose temporary lives as long as
       the reference it initializes (a declared variable's), not to the
       end of the full-expression */
    const semantic_node *extended_temporary = nullptr;
    /* the destruction of the temporary of the prvalue n: at the end of
       the full-expression, or with the block's variables */
    void destroyed_temporary(const semantic_node &n, entity_index destructor,
                             uint32_t temporary, ast_qualified_type t,
                             token_index at, uint32_t flag = 0);
    unsigned conditional_depth = 0;
    owned_array<uint32_t> temporaries, destructions;
    uint32_t conditional(const semantic_node &n);
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
    uint32_t block(const owned_array<uint32_t> &items)
    {
        return block(items.data(), static_cast<uint32_t>(items.size()));
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
                              int64_t &adjustment) const;
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
    uint32_t assignment(const semantic_node &n);
    uint32_t call(const semantic_node &n);
    uint32_t bind_reference(const semantic_node &n);
    uint32_t initializer_list(const semantic_node &n);
    uint32_t compound_literal(uint32_t n);
    uint32_t compound_literal_of(uint32_t value, ast_qualified_type t);
    /* whether an object of type t has a pointer to data member in it,
       whose null value is not all zero bits */
    bool holds_data_member_pointer(qualified_type t) const;
    bool static_zero_initialization(const entity &x, uint32_t s,
                                    token_index at);
    void address_taken(uint32_t lvalue);

    /* statements */
    uint32_t statement(uint32_t n);
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
    case fundamental_type::float128:
        /* as C: a type of its own on x86-64, long double elsewhere */
        if (target == PP_LINUX_X86_64)
            return ast_builder_type_basic(AST_TYPE_FLOAT128);
        return ast_builder_type_basic(AST_TYPE_LONG_DOUBLE);
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
            return !x.name && !x.linkage_name &&
                   !visible_local_class(analysis.entity_table, r.entity);
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
    case type_kind::block_pointer: /* the block's address, as C's ^ */
        made = ast_builder_type_pointer(c, type(r.operand, at));
        break;
    case type_kind::array:
        made = ast_builder_type_array(c, type(r.operand, at), r.bound,
                                      r.bound_known);
        break;
    case type_kind::atomic:
        made = ast_builder_type_atomic(c, type(r.operand, at));
        break;
    case type_kind::complex:
        made = ast_builder_type_complex(c, type(r.operand, at));
        break;
    case type_kind::ext_vector:
        made = ast_builder_type_ext_vector(c, type(r.operand, at), r.bound);
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
    case type_kind::pointer_to_member:
        /* [abi:data-member-pointers]: "The basic ABI properties of data
           member pointer types are those of ptrdiff_t." */
        if (record(r.operand).kind == type_kind::function)
            made = member_function_pointer_type();
        else
            made = ast_builder_type_ptrdiff(c);
        break;
    case type_kind::placeholder:
        /* [dcl.spec.auto]/11: a name of an entity whose type has an
           undeduced placeholder is in no expression, so the analysis
           leaves none for code */
        fail("a type with an undeduced placeholder", at);
        return ast_builder_type_basic(AST_TYPE_INT);
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
        if (analysis.empty_class(record(v.type).entity))
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
    /* [dcl.align]: the class's alignment-specifiers, as C's aligned on
       its first field (at offset 0), which raises the struct's alignment
       and rounds its size */
    if (x.requested_alignment && !aligned.empty() &&
        aligned[0] < x.requested_alignment)
        aligned[0] = x.requested_alignment;
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
    qualified_type object = qualified_type::make(
        analysis.entity_table[x.enclosing].type.unqualified(),
        object_cv(x.member_cv));
    return member_function_type(object, x.type, at);
}

ast_qualified_type c_lowering::member_function_type(qualified_type object,
                                                    qualified_type function,
                                                    token_index at)
{
    const struct type &f = record(function);
    owned_array<ast_qualified_type> parameters;
    parameters.append(ast_builder_type_pointer(c, type(object, at)));
    const qualified_type *p =
        analysis.type_table.parameters(function.unqualified());
    for (uint32_t i = 0; i < f.parameter_count; i++)
        parameters.append(parameter_type(p[i], at));
    return ast_builder_type_function(c, type(f.operand, at), parameters.data(),
                                     f.parameter_count + 1, f.variadic);
}

/* ---- symbols -------------------------------------------------------- */

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

/* the storage and flags of the symbol of e, a function or a variable not
   local to a function */
void c_lowering::symbol_linkage(entity_index e, int &storage,
                                uint32_t &flags) const
{
    const entity &x = analysis.entity_table[e];
    storage = AST_STORAGE_NONE;
    if (x.linkage_of_name == linkage::internal &&
        !analysis.specialization_visible(e))
        storage = AST_STORAGE_STATIC;
    /* [impl.clang]: an entity whose type has no linkage (an unnamed
       class's) cannot be named in another translation unit, so its symbol
       is local, its name unmangled as an external one's */
    else if (x.defined && without_linkage(x.type))
        storage = AST_STORAGE_STATIC;
    else if (!x.defined && x.kind == entity_kind::variable)
        storage = AST_STORAGE_EXTERN;
    flags = AST_SYMBOL_FLAG_GLOBAL;
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
       outermost function the class is in is inline or templated (then
       its copies link as one); so is a member of a closure type of
       namespace scope, unless an inline variable's initializer has it */
    if ((local_class_function(e) ||
         (x.enclosing && analysis.entity_table[x.enclosing].lambda)) &&
        !visible_local_class(analysis.entity_table, x.enclosing)) {
        storage = AST_STORAGE_STATIC;
        flags = AST_SYMBOL_FLAG_GLOBAL;
    }
}

/* the C symbol of a function or variable, made at its first use: named by
   its symbol (itanium_symbol_name) if it has one, by its identifier if it
   is automatic */
uint32_t c_lowering::symbol(entity_index e, token_index at, int variant)
{
    const entity &x = analysis.entity_table[e];
    if (x.unsupported_body && !declaring_symbols) {
        /* [temp.inst]/4: the definition of a member function used: its body
           has a construct the analysis has no rules for */
        const unsupported_construct &u =
            analysis.body_failures[x.unsupported_body - 1];
        failure.subclause = u.subclause;
        failure.file = u.file;
        failure.line = u.line;
        failure.function = u.function;
        failure.front_end = true;
        return fail("a function whose body is not supported",
                    u.node.first_token());
    }
    uint32_t *slot = &symbols[e];
    if (x.special != special_member::none && variant == 1)
        slot = &complete_symbols[e];
    if (x.special == special_member::destructor && variant == 0)
        slot = &deleting_symbols[e];
    if (*slot)
        return *slot;
    if (x.kind != entity_kind::function && x.kind != entity_kind::variable)
        return fail("an entity that is not a function or variable", at);
    /* [dcl.fct.def.delete]/2: a deleted function is never called in a
       well-formed program (a deleted destructor's object destroyed: the
       analysis does not see each destruction) */
    if (x.kind == entity_kind::function && x.deleted)
        return fail("a call of a deleted function", at);
    if (x.kind == entity_kind::variable && x.nonstatic_member)
        return fail("a non-static data member as an object", at);
    ast_qualified_type t = entity_type(e, at);
    if (variant == 2 && with_virtual_bases(e))
        t = structor_type(e, at);
    /* [basic.start.static]/2: an object of static storage duration
       initialized dynamically is written at start-up, so not in read-only
       memory whatever its type's cv-qualifiers; nor is one whose class
       has a destructor that runs, [class.dtor]/5: "const and volatile
       semantics are not applied on an object under destruction"; nor is
       an object with a mutable subobject, [dcl.stc]/8 */
    qualified_type element;
    if (x.kind == entity_kind::variable && !x.automatic && x.definition &&
        (x.type.cv() & cv_const) &&
        (!constant_initializer(x.definition) ||
         element_destructor(x.type, element) || has_mutable_subobject(x.type)))
        t = type(
            qualified_type::make(x.type.unqualified(), x.type.cv() & ~cv_const),
            at);
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
        return fail("an entity whose symbol is not mangled", at);
    int kind = AST_SYMBOL_VARIABLE;
    if (x.kind == entity_kind::function)
        kind = AST_SYMBOL_FUNCTION;
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    entity_index function = enclosing_function(e);
    if (function)
        symbol_linkage(function, storage, flags);
    /* [dcl.inline]/6: "A static local variable in an inline function with
       external or module linkage always refers to the same object": a
       static local of a function emitted weak in every translation unit
       is weak too (clang's linkonce_odr) */
    if (function && kind == AST_SYMBOL_VARIABLE &&
        storage == AST_STORAGE_NONE && (flags & AST_SYMBOL_FLAG_LINKONCE))
        flags = AST_SYMBOL_FLAG_GLOBAL | AST_SYMBOL_FLAG_LINKONCE |
                AST_SYMBOL_FLAG_WEAK;
    else if (function) {
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
    if (!function)
        symbol_linkage(e, storage, flags);
    s = ast_builder_symbol(c, kind, name.data(), name.size(), t, storage, flags,
                           0);
    *slot = s;
    if (x.asm_label) {
        /* [ext:gnu_asm_labels] the symbol is the label's characters, as
           written (codegen writes an assembler name as given): the
           characters between each piece's quotes (sema took no escape
           sequence) */
        owned_buffer written, label;
        tokens.spelling(x.asm_label, written.get());
        bool inside = false;
        for (size_t i = 0; i < written.size(); i++) {
            if (written.data()[i] == '"')
                inside = !inside;
            else if (inside)
                vbuffer_append(label.get(), written.data() + i, 1);
        }
        ast_builder_symbol_assembler_name(c, s, label.data(), label.size());
    }
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
    /* [ext:gnu_attributes] a class's visibility("hidden") is its
       members' (and its nested classes') */
    bool hidden = x.gnu_flags & gnu_hidden_visibility;
    for (entity_index k = x.enclosing;
         !hidden && k &&
         analysis.entity_table[k].kind == entity_kind::class_type;
         k = analysis.entity_table[k].enclosing)
        hidden = analysis.entity_table[k].gnu_flags & gnu_hidden_visibility;
    if (hidden)
        ast_builder_symbol_visibility(c, s, "hidden", 6);
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
                out.append(if_constructed(loop, r.flag));
            continue;
        }
        uint32_t object = c_variable(c, r.symbol, r.type);
        uint32_t call = special_call(
            r.destructor,
            address_of(object, ast_builder_type_pointer(c, r.type)), nullptr, 0,
            at);
        out.append(if_constructed(c_statement(c, call), r.flag));
    }
}

uint32_t c_lowering::each_element(uint32_t s, ast_qualified_type array,
                                  ast_qualified_type element, uint64_t bound,
                                  bool reverse, entity_index e, token_index at)
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type index_type = ast_builder_type_basic(AST_TYPE_LONG);
    ast_qualified_type truth = ast_builder_type_basic(AST_TYPE_INT);
    uint32_t i = c_local(c, index_type);
    auto index = [&] { return c_variable(c, i, index_type); };
    auto constant = [&](uint64_t value) {
        return ast_builder_integer(c, index_type, static_cast<int64_t>(value));
    };
    /* the elements of an array of arrays too, as one sequence
       ([dcl.array]/6: contiguous) */
    ast_qualified_type pointer = ast_builder_type_pointer(c, element);
    uint32_t first = cast(
        address_of(c_variable(c, s, array), ast_builder_type_pointer(c, array)),
        pointer, AST_CAST_POINTER_TO_POINTER);
    uint32_t object = make(AST_NODE_INDEX, element, first, index(), 0, 0,
                           AST_NODE_FLAG_LVALUE);
    uint32_t call =
        special_call(e, address_of(object, pointer), nullptr, 0, at);
    if (!call)
        return 0;
    uint32_t body = c_statement(c, call);
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
        c_declare(c, i),
        ast_builder_node(c, AST_NODE_FOR, v, c_statement(c, init), cond,
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

uint32_t c_lowering::constructed_flag()
{
    if (!conditional_depth)
        return 0;
    ast_qualified_type b = ast_builder_type_basic(AST_TYPE_BOOL);
    uint32_t flag = c_local(c, b);
    temporaries.append(c_declare(c, flag, ast_builder_integer(c, b, 0)));
    return flag;
}

uint32_t c_lowering::mark_constructed(uint32_t made, uint32_t flag)
{
    if (!flag || !made)
        return made;
    ast_qualified_type b = ast_builder_type_basic(AST_TYPE_BOOL);
    uint32_t set = make(AST_NODE_ASSIGN, b, c_variable(c, flag, b),
                        ast_builder_integer(c, b, 1), 0, 0, 0);
    return make(AST_NODE_COMMA, b, made, set, 0, 0, 0);
}

uint32_t c_lowering::if_constructed(uint32_t statement, uint32_t flag)
{
    if (!flag)
        return statement;
    ast_qualified_type b = ast_builder_type_basic(AST_TYPE_BOOL);
    return ast_builder_node(c, AST_NODE_IF,
                            ast_builder_type_basic(AST_TYPE_VOID),
                            c_variable(c, flag, b), statement, 0, 0, 0);
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
    if (!in_function)
        return fail("dynamic initialization of a variable", n.at);
    ast_qualified_type t = type(n.type, n.at);
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    uint32_t object = c_variable(c, temporary, t);
    uint32_t call = construct_at(n, object, 0, n.at);
    if (!call)
        return 0;
    uint32_t statements[3] = {c_declare(c, temporary), c_statement(c, call),
                              ast_builder_node(c, AST_NODE_EXPRESSION, t,
                                               c_variable(c, temporary, t), 0,
                                               0, 0, 0)};
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
    uint32_t symbol = c_local(c, t);
    items.append(c_declare(c, symbol));
    uint32_t variable = c_variable(c, symbol, t);
    items.append(
        c_statement(c, make(AST_NODE_ASSIGN, t, variable, lowered, 0, 0, 0)));
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
    auto variable = [&] { return c_variable(c, symbol, t); };
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
                else if (r.kind == type_kind::pointer_to_member)
                    value = null_member_pointer(object, n.at);
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
        uint32_t statement = c_statement(c, done);
        if (n.c)
            statement = ast_builder_node(c, AST_NODE_IF, v, truth(variable()),
                                         statement, 0, 0, 0);
        items.append(statement);
    }
    items.append(
        ast_builder_node(c, AST_NODE_EXPRESSION, t, variable(), 0, 0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, t, block(items), 0, 0, 0, 0);
}

uint32_t c_lowering::element_loop(uint32_t first, ast_qualified_type object,
                                  ast_qualified_type size, uint32_t count,
                                  bool reverse, uint32_t each)
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type truth = ast_builder_type_basic(AST_TYPE_INT);
    ast_qualified_type pointer = ast_builder_type_pointer(c, object);
    uint32_t i = c_local(c, size);
    uint32_t element = c_local(c, pointer);
    auto index = [&] { return c_variable(c, i, size); };
    auto counted = [&] { return c_variable(c, count, size); };
    uint32_t outer = allocated_symbol;
    ast_qualified_type outer_type = allocated_type;
    allocated_symbol = element;
    allocated_type = pointer;
    uint32_t done = expression(each);
    allocated_symbol = outer;
    allocated_type = outer_type;
    if (!done)
        return 0;
    uint32_t at_index =
        make(AST_NODE_INDEX, object, c_variable(c, first, pointer),
             to_ptrdiff(index()), 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t body_items[2] = {
        c_statement(c, make(AST_NODE_ASSIGN, pointer,
                            c_variable(c, element, pointer),
                            address_of(at_index, pointer), 0, 0, 0)),
        c_statement(c, done)};
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
        c_declare(c, i), c_declare(c, element),
        ast_builder_node(c, AST_NODE_FOR, v, c_statement(c, init), cond,
                         ast_builder_list(c, step_body, 2), 0, 0)};
    return block(items, 3);
}

/* a variable symbol declared in items, assigned value if any */
static uint32_t declared_symbol(cc *c, owned_array<uint32_t> &items,
                                ast_qualified_type t)
{
    uint32_t s = c_local(c, t);
    items.append(c_declare(c, s));
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
        return c_variable(c, s, st);
    };
    uint32_t p = declared_symbol(c, items, t);
    items.append(c_statement(
        c, make(AST_NODE_ASSIGN, t, variable(p, t),
                cast(variable(raw, bytes), t, AST_CAST_POINTER_TO_POINTER), 0,
                0, 0)));
    owned_array<uint32_t> body;
    if (n.value) {
        ast_qualified_type counts = ast_builder_type_pointer(c, size);
        uint32_t stored = make(
            AST_NODE_DEREFERENCE, size,
            cast(variable(raw, bytes), counts, AST_CAST_POINTER_TO_POINTER), 0,
            0, 0, AST_NODE_FLAG_LVALUE);
        body.append(c_statement(c, make(AST_NODE_ASSIGN, size, stored,
                                        variable(count, size), 0, 0, 0)));
        body.append(c_statement(
            c, make(AST_NODE_ASSIGN, t, variable(p, t),
                    offset_pointer(variable(raw, bytes),
                                   static_cast<int64_t>(n.value), t),
                    0, 0, 0)));
    }
    if (n.b) {
        uint32_t loop = element_loop(p, object, size, count, false, n.b);
        if (!loop)
            return 0;
        body.append(loop);
    }
    if (!body.empty()) {
        uint32_t made = block(body);
        if (n.c)
            made = ast_builder_node(c, AST_NODE_IF, v,
                                    truth(variable(raw, bytes)), made, 0, 0, 0);
        items.append(made);
    }
    items.append(ast_builder_node(c, AST_NODE_EXPRESSION, t, variable(p, t), 0,
                                  0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, t, block(items), 0, 0, 0, 0);
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
        return c_variable(c, s, st);
    };
    owned_array<uint32_t> body;
    uint32_t raw = declared_symbol(c, body, bytes);
    body.append(c_statement(
        c,
        make(AST_NODE_ASSIGN, bytes, variable(raw, bytes),
             offset_pointer(variable(p, t), -static_cast<int64_t>(n.d), bytes),
             0, 0, 0)));
    uint32_t count = declared_symbol(c, body, size);
    uint32_t stored =
        make(AST_NODE_DEREFERENCE, size,
             cast(variable(raw, bytes), ast_builder_type_pointer(c, size),
                  AST_CAST_POINTER_TO_POINTER),
             0, 0, 0, AST_NODE_FLAG_LVALUE);
    body.append(c_statement(c, make(AST_NODE_ASSIGN, size,
                                    variable(count, size), stored, 0, 0, 0)));
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
    body.append(c_statement(c, deallocated));
    items.append(ast_builder_node(c, AST_NODE_IF, v, truth(variable(p, t)),
                                  block(body), 0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, v, block(items), 0, 0, 0, 0);
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
    auto variable = [&] { return c_variable(c, symbol, t); };
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
    return make(AST_NODE_STATEMENT_EXPRESSION, t, block(items), 0, 0, 0, 0);
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
    auto variable = [&] { return c_variable(c, symbol, t); };
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
        body[0] = c_statement(c, call);
        items.append(ast_builder_node(c, AST_NODE_IF, v, truth(variable()),
                                      block(body, 1), 0, 0, 0));
        return make(AST_NODE_STATEMENT_EXPRESSION, v, block(items), 0, 0, 0, 0);
    }
    if (n.b)
        destroyed = expression(n.b);
    deallocated = expression(n.c);
    allocated_symbol = outer;
    allocated_type = outer_type;
    if ((n.b && !destroyed) || !deallocated)
        return 0;
    if (destroyed)
        body[count++] = c_statement(c, destroyed);
    body[count++] = c_statement(c, deallocated);
    items.append(ast_builder_node(c, AST_NODE_IF, v, truth(variable()),
                                  block(body, count), 0, 0, 0));
    return make(AST_NODE_STATEMENT_EXPRESSION, v, block(items), 0, 0, 0, 0);
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

/* a glvalue as a C lvalue.  [expr.ass]/1 and [expr.pre.incr]/1: an
   assignment's or prefix increment's result is its left operand, an
   lvalue, which C's is not: *({ T *p; p = &lhs; *p op= rhs; p; }), the
   left operand evaluated once */
uint32_t c_lowering::object(uint32_t n)
{
    if (node(n).kind == semantic_kind::conditional && !is_glvalue(node(n)) &&
        record(node(n).type).kind == type_kind::class_type)
        return temporary_object(n);
    uint32_t made = expression(n);
    if (!made)
        return 0;
    const struct node &x = ast_builder_ast(c)->nodes[made];
    if ((x.flags & AST_NODE_FLAG_LVALUE) ||
        record(node(n).type).kind == type_kind::function)
        return made;
    bool assigns = x.op == AST_NODE_ASSIGN || x.op == AST_NODE_PRE_INCREMENT ||
                   x.op == AST_NODE_PRE_DECREMENT;
    if (!assigns || !in_function)
        return fail("an assignment or increment used as an object", node(n).at);
    uint32_t left = x.a;
    ast_qualified_type t = type_of(left);
    ast_qualified_type pointer = ast_builder_type_pointer(c, t);
    uint32_t symbol = c_local(c, pointer);
    auto variable = [&] { return c_variable(c, symbol, pointer); };
    uint32_t items[4];
    items[0] = c_declare(c, symbol);
    items[1] = c_statement(c, make(AST_NODE_ASSIGN, pointer, variable(),
                                   address_of(left, pointer), 0, 0, 0));
    /* the assignment, through p */
    uint32_t through = make(AST_NODE_DEREFERENCE, t, variable(), 0, 0, 0,
                            AST_NODE_FLAG_LVALUE);
    ast_builder_ast(c)->nodes[made].a = through;
    items[2] = c_statement(c, made);
    items[3] = ast_builder_node(c, AST_NODE_EXPRESSION, pointer, variable(), 0,
                                0, 0, 0);
    uint32_t address = make(AST_NODE_STATEMENT_EXPRESSION, pointer,
                            block(items, 4), 0, 0, 0, 0);
    return make(AST_NODE_DEREFERENCE, t, address, 0, 0, 0,
                AST_NODE_FLAG_LVALUE);
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
    const literal_value &v = tokens.files.value_of(t);
    owned_array<uint8_t> bytes;
    uint32_t count = v.string_length + 1;
    bytes.resize(static_cast<size_t>(count) * size);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t u = 0;
        if (i < v.string_length)
            u = units[v.string_offset + i];
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
        if (record(node(n.a).type).kind == type_kind::pointer_to_member)
            return member_pointer_test(n.a, n.at);
        return cast(expression(n.a), t, AST_CAST_BOOL);
    case conversion_kind::null_member_pointer:
    case conversion_kind::member_pointer_to_derived:
    case conversion_kind::member_pointer_to_base:
        return member_pointer_conversion(n);
    case conversion_kind::member_pointer_qualification:
        return expression(n.a);
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
            if ((structor_variant == 1 || structor_variant == 3) &&
                k == structor_class && at_this(n.a)) {
                uint32_t p = expression(n.a);
                if (!p)
                    return 0;
                return offset_pointer(
                    p, static_cast<int64_t>(analysis.entity_table[n.b].offset),
                    t);
            }
            /* the pointer read three times: a temporary holds one with
               side effects */
            reread r;
            uint32_t first =
                read_first(n.a, r,
                           "a pointer to a virtual base of an expression with "
                           "side effects outside a statement");
            if (!first)
                return 0;
            uint32_t second = read_again(r);
            uint32_t third = read_again(r);
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
        if (analysis.entity_table[n.b].offset)
            return null_checked_offset(
                n, static_cast<int64_t>(analysis.entity_table[n.b].offset), t);
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::base_to_derived:
        /* [expr.static.cast]/11: "the resulting pointer points to the
           enclosing object of type D": the base's offset subtracted */
        if (analysis.entity_table[n.b].offset)
            return null_checked_offset(
                n, -static_cast<int64_t>(analysis.entity_table[n.b].offset), t);
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::pointer_to_integral:
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_INTEGER);
    case conversion_kind::bit_cast:
        return cast(expression(n.a), t, AST_CAST_BIT_CAST);
    case conversion_kind::vector_convert:
        return ast_builder_convert_vector(c, t, expression(n.a));
    case conversion_kind::integral_to_pointer:
        return cast(expression(n.a), t, AST_CAST_INTEGER_TO_POINTER);
    case conversion_kind::to_void:
        return cast(discard(n.a), ast_builder_type_basic(AST_TYPE_VOID),
                    AST_CAST_VOID);
    }
    return fail("a conversion", n.at);
}

/* ---- pointers to members ([dcl.mptr]) ------------------------------- */

/*
 * [abi:member-function-pointers]: "the basic ABI properties of member
 * function pointer types are those of the following class, where fnptr_t
 * is the appropriate function-pointer type for a member function of this
 * type: struct { fnptr_t ptr; ptrdiff_t adj; };": one C struct, ptr a
 * void *.  The target (AArch64) has the 32-bit ARM representation: "the
 * this-adjustment stored in adj is left-shifted by one, and the low bit of
 * adj indicates whether ptr is a function pointer (including null) or the
 * offset of a v-table entry."
 */
ast_qualified_type c_lowering::member_function_pointer_type()
{
    if (member_function_pair)
        return member_function_pair;
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type fields[2] = {ast_builder_type_pointer(c, v),
                                    ast_builder_type_ptrdiff(c)};
    const char *names[2] = {"ptr", "adj"};
    size_t lengths[2] = {3, 3};
    uint32_t aligned[2] = {0, 0};
    uint8_t packed[2] = {0, 0};
    int32_t widths[2] = {-1, -1};
    member_function_pair = ast_builder_type_record(c, "", 0, false);
    ast_builder_type_record_complete(c, member_function_pair, fields, names,
                                     lengths, aligned, packed, widths, 2);
    return member_function_pair;
}

bool c_lowering::holds_data_member_pointer(qualified_type t) const
{
    const struct type &r = record(t);
    if (r.kind == type_kind::array)
        return holds_data_member_pointer(r.operand);
    if (r.kind == type_kind::pointer_to_member)
        return record(r.operand).kind != type_kind::function;
    if (r.kind != type_kind::class_type)
        return false;
    const entity &x = analysis.entity_table[r.entity];
    for (uint32_t i = 0; i < x.member_count; i++)
        if (holds_data_member_pointer(
                analysis
                    .entity_table[analysis.class_members[x.first_member + i]]
                    .type))
            return true;
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        if (holds_data_member_pointer(
                analysis
                    .entity_table
                        [analysis.class_virtual_bases[x.first_virtual_base + i]]
                    .type))
            return true;
    return false;
}

bool c_lowering::member_pointer_value(uint32_t n, entity_index &member,
                                      int64_t &adjustment) const
{
    adjustment = 0;
    for (;;) {
        const semantic_node &x = node(n);
        if (x.kind == semantic_kind::member_pointer) {
            member = x.a;
            return true;
        }
        if (x.kind != semantic_kind::conversion)
            return false;
        switch (static_cast<conversion_kind>(x.op)) {
        case conversion_kind::null_member_pointer:
            member = 0;
            return true;
        case conversion_kind::member_pointer_to_derived:
            adjustment += static_cast<int64_t>(x.value);
            break;
        case conversion_kind::member_pointer_to_base:
            adjustment -= static_cast<int64_t>(x.value);
            break;
        case conversion_kind::member_pointer_qualification:
            break;
        default:
            return false;
        }
        n = x.a;
    }
}

/* {ptr, adj}: an initializer at namespace scope, an object in a
   function */
uint32_t c_lowering::member_function_pointer_of(uint32_t ptr, uint32_t adj,
                                                token_index at)
{
    ast_qualified_type t = member_function_pointer_type();
    uint32_t values[2] = {ptr, adj};
    uint32_t init = make(AST_NODE_INITIALIZER, t, 0,
                         ast_builder_list(c, values, 2), 2, 0, 0);
    if (!in_function)
        return init;
    return compound_literal_of(init, t);
}

/*
 * A member pointer constant: [expr.unary.op]/3.1's, or the null member
 * pointer value, through conversions.  [abi:data-member-pointers]: "A data
 * member pointer is represented as the data member's offset in bytes from
 * the address point of an object of the base type"; "A null data member
 * pointer is represented as an offset of -1."
 * [abi:member-function-pointers]: "A member function pointer for a
 * non-virtual member function is represented with ptr set to a pointer to
 * the function"; in the ARM representation "A virtual member function
 * pointer sets ptr to the v-table entry offset"; "A null member function
 * pointer sets ptr to a null function pointer and must ensure that the low
 * bit of adj is clear".
 */
uint32_t c_lowering::member_pointer(uint32_t n)
{
    const semantic_node &x = node(n);
    entity_index member = 0;
    int64_t adjustment = 0;
    if (!member_pointer_value(n, member, adjustment))
        return fail("a pointer to member", x.at);
    return member_pointer_constant(x.type, member, adjustment, x.at);
}

uint32_t c_lowering::member_pointer_constant(qualified_type t,
                                             entity_index member,
                                             int64_t adjustment, token_index at)
{
    if (!member)
        return null_member_pointer(t, at);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    const entity &f = analysis.entity_table[member];
    if (record(record(t).operand).kind != type_kind::function)
        return ast_builder_integer(c, d,
                                   static_cast<int64_t>(f.offset) + adjustment);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    uint32_t ptr = 0;
    int64_t adj = 2 * adjustment;
    if (f.virtual_) {
        ptr = cast(ast_builder_integer(
                       c, d,
                       static_cast<int64_t>(f.vtable_slot) *
                           static_cast<int64_t>(analysis.pointer_size())),
                   vp, AST_CAST_INTEGER_TO_POINTER);
        adj |= 1;
    } else {
        uint32_t s = symbol(member, at);
        if (!s)
            return 0;
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
        ast_qualified_type ft = entity_type(member, at);
        ptr =
            cast(cast(make(AST_NODE_VARIABLE, ft, s, 0, 0, 0, 0),
                      ast_builder_type_pointer(c, ft), AST_CAST_FUNCTION_DECAY),
                 vp, AST_CAST_POINTER_TO_POINTER);
    }
    return member_function_pointer_of(ptr, ast_builder_integer(c, d, adj), at);
}

uint32_t c_lowering::null_member_pointer(qualified_type t, token_index at)
{
    if (record(record(t).operand).kind != type_kind::function)
        return ast_builder_integer(c, ast_builder_type_ptrdiff(c), -1);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    uint32_t zero =
        ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0);
    return member_function_pointer_of(
        cast(zero, ast_builder_type_pointer(c, v), AST_CAST_NULL_POINTER),
        ast_builder_integer(c, ast_builder_type_ptrdiff(c), 0), at);
}

uint32_t c_lowering::read_first(uint32_t n, reread &r, const char *what)
{
    r = reread();
    r.node = n;
    uint32_t p = expression(n);
    if (!p)
        return 0;
    r.type = AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(p));
    if (side_effect_free(p) || rereadable_member_pointer(n))
        return p;
    if (!collecting)
        return fail(what, node(n).at);
    r.symbol = full_expression_temporary(r.type);
    return make(AST_NODE_COMMA, r.type,
                make(AST_NODE_ASSIGN, r.type, read_again(r), p, 0, 0, 0),
                read_again(r), 0, 0, 0);
}

/* a member pointer constant, or a variable's value, through member pointer
   conversions: lowered as new objects (compound literals) each time, with
   no effect */
bool c_lowering::rereadable_member_pointer(uint32_t n) const
{
    for (;;) {
        const semantic_node &x = node(n);
        if (x.kind == semantic_kind::member_pointer)
            return true;
        if (x.kind != semantic_kind::conversion)
            return false;
        switch (static_cast<conversion_kind>(x.op)) {
        case conversion_kind::null_member_pointer:
            return true;
        case conversion_kind::lvalue_to_rvalue:
            return node(x.a).kind == semantic_kind::entity;
        case conversion_kind::member_pointer_to_derived:
        case conversion_kind::member_pointer_to_base:
        case conversion_kind::member_pointer_qualification:
            n = x.a;
            break;
        default:
            return false;
        }
    }
}

uint32_t c_lowering::read_again(const reread &r)
{
    if (!r.symbol)
        return expression(r.node);
    return c_variable(c, r.symbol, r.type);
}

/*
 * [conv.mem]/1, /2 and [expr.static.cast]/12: a constant is folded
 * (member_pointer); otherwise [abi:data-member-pointers]: "Base-to-derived
 * and derived-to-base conversions of a non-null data member pointer can be
 * performed by adding or subtracting (respectively) the static offset of
 * the base within the derived class" (p == -1 ? -1 : p + offset), and
 * [abi:member-function-pointers]: "in the 32-bit ARM representation, the
 * addend must be left-shifted by one.  Because the adjustment does not
 * factor into whether a member function pointer is null, this addition can
 * be done unconditionally when performing a conversion."
 */
uint32_t c_lowering::member_pointer_conversion(const semantic_node &n)
{
    if (static_cast<conversion_kind>(n.op) ==
        conversion_kind::null_member_pointer)
        return null_member_pointer(n.type, n.at);
    int64_t offset = static_cast<int64_t>(n.value);
    if (static_cast<conversion_kind>(n.op) ==
        conversion_kind::member_pointer_to_base)
        offset = -offset;
    entity_index member = 0;
    int64_t adjustment = 0;
    if (member_pointer_value(n.a, member, adjustment))
        return member_pointer_constant(n.type, member, adjustment + offset,
                                       n.at);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (record(record(n.type).operand).kind == type_kind::function) {
        /* {p.ptr, p.adj + 2 * offset}, p read twice */
        if (!offset)
            return expression(n.a);
        ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
        reread r;
        uint32_t first = read_first(n.a, r,
                                    "a pointer to member function converted "
                                    "outside a statement");
        if (!first)
            return 0;
        uint32_t held = first;
        if (r.symbol)
            held = read_again(r);
        uint32_t adj =
            make(AST_NODE_BINARY, d, ast_builder_member(c, read_again(r), 1, d),
                 ast_builder_integer(c, d, 2 * offset), 0, LEX_TOKEN_PLUS, 0);
        uint32_t pair = member_function_pointer_of(
            ast_builder_member(c, held, 0, ast_builder_type_pointer(c, v)), adj,
            n.at);
        if (!r.symbol)
            return pair;
        /* the temporary assigned before the pair reads it */
        return make(AST_NODE_COMMA, r.type, first, pair, 0, 0, 0);
    }
    if (!offset)
        return expression(n.a);
    reread r;
    uint32_t first = read_first(n.a, r,
                                "a pointer to data member converted "
                                "outside a statement");
    if (!first)
        return 0;
    uint32_t null = ast_builder_integer(c, d, -1);
    uint32_t test =
        make(AST_NODE_BINARY, ast_builder_type_basic(AST_TYPE_INT), first,
             ast_builder_integer(c, d, -1), 0, LEX_TOKEN_EQUAL_EQUAL, 0);
    uint32_t moved =
        make(AST_NODE_BINARY, d, read_again(r),
             ast_builder_integer(c, d, offset), 0, LEX_TOKEN_PLUS, 0);
    return make(AST_NODE_CONDITIONAL, d, test, null, moved, 0, 0);
}

/*
 * [expr.mptr.oper]/2: "The result is an object ... of the type specified
 * by the second operand": *(T *)((char *)object + p), the object's address
 * first ([expr.mptr.oper]/4: "the expression E1 is sequenced before the
 * expression E2").
 */
uint32_t c_lowering::member_pointer_object(const semantic_node &n)
{
    if (record(n.type).kind == type_kind::function)
        return fail("a member function through a pointer to member outside "
                    "a call",
                    n.at);
    uint32_t object = expression(n.a);
    uint32_t offset = expression(n.b);
    if (!object || !offset)
        return 0;
    if (!side_effect_free(object) && !side_effect_free(offset))
        /* C's operands are unsequenced */
        return fail("a pointer to data member applied with effects in both "
                    "operands",
                    n.at);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = ast_builder_type_pointer(c, byte);
    ast_qualified_type t = type(n.type, n.at);
    uint32_t at = make(AST_NODE_INDEX, byte,
                       cast(object, bytes, AST_CAST_POINTER_TO_POINTER),
                       to_ptrdiff(offset), 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t address =
        cast(make(AST_NODE_ADDRESS, bytes, at, 0, 0, 0, 0),
             ast_builder_type_pointer(c, t), AST_CAST_POINTER_TO_POINTER);
    return make(AST_NODE_DEREFERENCE, t, address, 0, 0, 0,
                AST_NODE_FLAG_LVALUE);
}

/*
 * [conv.bool]/1: "A zero value, null pointer value, or null member pointer
 * value is converted to false": [abi:data-member-pointers] p != -1;
 * [abi:member-function-pointers]: "A member function pointer is null if
 * ptr is equal to a null function pointer and (only when using the 32-bit
 * ARM representation) the low bit of adj is clear": p.ptr != 0 || (p.adj &
 * 1).
 */
uint32_t c_lowering::member_pointer_test(uint32_t n, token_index at)
{
    ast_qualified_type i = ast_builder_type_basic(AST_TYPE_INT);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (record(record(node(n).type).operand).kind != type_kind::function) {
        uint32_t p = expression(n);
        if (!p)
            return 0;
        return to_bool(make(AST_NODE_BINARY, i, p,
                            ast_builder_integer(c, d, -1), 0,
                            LEX_TOKEN_EXCLAMATION_EQUAL, 0));
    }
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    reread r;
    uint32_t first = read_first(n, r,
                                "a pointer to member function tested "
                                "outside a statement");
    if (!first)
        return 0;
    uint32_t ptr = ast_builder_member(c, first, 0, vp);
    uint32_t adj = ast_builder_member(c, read_again(r), 1, d);
    uint32_t odd = make(AST_NODE_BINARY, d, adj, ast_builder_integer(c, d, 1),
                        0, LEX_TOKEN_AMPERSAND, 0);
    return to_bool(make(AST_NODE_LOGICAL_OR, i, ptr, odd, 0, 0, 0));
}

/*
 * [expr.eq]/4: "If two pointers to members are both the null member
 * pointer value, they compare equal. ... Otherwise, they compare equal if
 * and only if they would refer to the same member of the same most derived
 * object or the same subobject if indirection with a hypothetical object
 * of the associated class type were performed".  [abi:data-member-pointers]
 * as integers; [abi:member-function-pointers]: "Two member function
 * pointers are equal if they are both null or if their corresponding
 * values of ptr and adj are equal": a.ptr == b.ptr && (a.adj == b.adj ||
 * (a.ptr == 0 && ((a.adj | b.adj) & 1) == 0)).
 */
uint32_t c_lowering::member_pointer_equality(const semantic_node &n, int op)
{
    ast_qualified_type i = ast_builder_type_basic(AST_TYPE_INT);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (record(record(node(n.a).type).operand).kind != type_kind::function) {
        uint32_t a = expression(n.a), b = expression(n.b);
        if (!a || !b)
            return 0;
        return to_bool(
            make(AST_NODE_BINARY, i, a, b, 0, static_cast<uint16_t>(op), 0));
    }
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    const char *what = "pointers to member functions compared outside a "
                       "statement";
    reread ra, rb;
    uint32_t a = read_first(n.a, ra, what);
    if (!a)
        return 0;
    uint32_t b = read_first(n.b, rb, what);
    if (!b)
        return 0;
    auto binary_int = [&](uint32_t x, uint32_t y, int o) {
        return make(AST_NODE_BINARY, i, x, y, 0, static_cast<uint16_t>(o), 0);
    };
    uint32_t same_ptr =
        binary_int(ast_builder_member(c, a, 0, vp),
                   ast_builder_member(c, b, 0, vp), LEX_TOKEN_EQUAL_EQUAL);
    uint32_t same_adj = binary_int(ast_builder_member(c, read_again(ra), 1, d),
                                   ast_builder_member(c, read_again(rb), 1, d),
                                   LEX_TOKEN_EQUAL_EQUAL);
    uint32_t null_ptr =
        make(AST_NODE_UNARY, i, ast_builder_member(c, read_again(ra), 0, vp), 0,
             0, LEX_TOKEN_EXCLAMATION, 0);
    uint32_t both =
        make(AST_NODE_BINARY, d, ast_builder_member(c, read_again(ra), 1, d),
             ast_builder_member(c, read_again(rb), 1, d), 0, LEX_TOKEN_PIPE, 0);
    uint32_t even =
        binary_int(make(AST_NODE_BINARY, d, both, ast_builder_integer(c, d, 1),
                        0, LEX_TOKEN_AMPERSAND, 0),
                   ast_builder_integer(c, d, 0), LEX_TOKEN_EQUAL_EQUAL);
    uint32_t equal = make(
        AST_NODE_LOGICAL_AND, i, same_ptr,
        make(AST_NODE_LOGICAL_OR, i, same_adj,
             make(AST_NODE_LOGICAL_AND, i, null_ptr, even, 0, 0, 0), 0, 0, 0),
        0, 0, 0);
    if (op == LEX_TOKEN_EXCLAMATION_EQUAL)
        equal = make(AST_NODE_UNARY, i, equal, 0, 0, LEX_TOKEN_EXCLAMATION, 0);
    return to_bool(equal);
}

/*
 * [expr.mptr.oper]/6: (E1.*E2)(args), as [abi:member-function-pointers]
 * says: "A call is performed as follows: Add the stored adjustment to the
 * this address.  If the member pointer stores a v-table entry offset,
 * load the v-table from the adjusted this address and call the v-table
 * entry at the stored offset.  Otherwise, call the stored function
 * pointer."  Into items: p = E2's value, o = (char *)E1's address + (p.adj
 * >> 1); the callee returned, (p.adj & 1 ? *(F **)(*(char **)o +
 * (ptrdiff_t)p.ptr) : (F *)p.ptr), and object o.
 */
uint32_t c_lowering::member_function_call(const semantic_node &n,
                                          owned_array<uint32_t> &items,
                                          uint32_t &object)
{
    const semantic_node &access = node(n.a);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type vp = ast_builder_type_pointer(c, v);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = ast_builder_type_pointer(c, byte);
    uint32_t address = held_value(access.a, bytes, items);
    if (!address)
        return 0;
    uint32_t pair = held_value(access.b, member_function_pointer_type(), items);
    if (!pair)
        return 0;
    ast_qualified_type t = member_function_pointer_type();
    auto read = [&](uint32_t s, ast_qualified_type type_) {
        return c_variable(c, s, type_);
    };
    auto adj = [&] { return ast_builder_member(c, read(pair, t), 1, d); };
    /* o = (char *)E1 + (p.adj >> 1) */
    uint32_t moved =
        make(AST_NODE_INDEX, byte, read(address, bytes),
             make(AST_NODE_BINARY, d, adj(), ast_builder_integer(c, d, 1), 0,
                  LEX_TOKEN_GREATER_GREATER, 0),
             0, 0, AST_NODE_FLAG_LVALUE);
    items.append(c_statement(
        c, make(AST_NODE_ASSIGN, bytes, read(address, bytes),
                make(AST_NODE_ADDRESS, bytes, moved, 0, 0, 0, 0), 0, 0, 0)));
    const struct type &pointer = record(node(access.b).type);
    qualified_type class_type_ = qualified_type::make(
        analysis.entity_table[pointer.entity].type.unqualified(), cv_none);
    ast_qualified_type ft =
        member_function_type(class_type_, pointer.operand, n.at);
    ast_qualified_type fp = ast_builder_type_pointer(c, ft);
    /* *(F **)(*(char **)o + (ptrdiff_t)p.ptr) */
    uint32_t table =
        make(AST_NODE_DEREFERENCE, bytes,
             cast(read(address, bytes), ast_builder_type_pointer(c, bytes),
                  AST_CAST_POINTER_TO_POINTER),
             0, 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t entry = make(AST_NODE_INDEX, byte, table,
                          cast(ast_builder_member(c, read(pair, t), 0, vp), d,
                               AST_CAST_POINTER_TO_INTEGER),
                          0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t virtual_callee =
        make(AST_NODE_DEREFERENCE, fp,
             cast(make(AST_NODE_ADDRESS, bytes, entry, 0, 0, 0, 0),
                  ast_builder_type_pointer(c, fp), AST_CAST_POINTER_TO_POINTER),
             0, 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t direct_callee = cast(ast_builder_member(c, read(pair, t), 0, vp),
                                  fp, AST_CAST_POINTER_TO_POINTER);
    uint32_t odd = make(AST_NODE_BINARY, d, adj(), ast_builder_integer(c, d, 1),
                        0, LEX_TOKEN_AMPERSAND, 0);
    object = cast(read(address, bytes),
                  ast_builder_type_pointer(c, type(class_type_, n.at)),
                  AST_CAST_POINTER_TO_POINTER);
    return make(AST_NODE_CONDITIONAL, fp, odd, virtual_callee, direct_callee, 0,
                0);
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
    if (record(node(n.a).type).kind == type_kind::pointer_to_member)
        return member_pointer_equality(n, op);
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
    const entity &x = analysis.entity_table[record(n.type).entity];
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    ast_qualified_type pointer = ast_builder_type_pointer(c, t);
    if (!(ast_builder_ast(c)->nodes[b].flags & AST_NODE_FLAG_LVALUE)) {
        /* a prvalue: materialized ([basic.lval]/7), its data size copied */
        uint32_t held = full_expression_temporary(t);
        b = make(
            AST_NODE_DEREFERENCE, t,
            make(AST_NODE_COMMA, pointer,
                 make(AST_NODE_ASSIGN, t, c_variable(c, held, t), b, 0, 0, 0),
                 address_of(c_variable(c, held, t), pointer), 0, 0, 0),
            0, 0, 0, AST_NODE_FLAG_LVALUE);
    }
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
    uint32_t temporary = c_local(c, pointer);
    temporaries.append(c_declare(c, temporary));
    auto p = [&] { return c_variable(c, temporary, pointer); };
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
    qualified_type function_type = node(n.a).type;
    uint32_t callee = 0;
    uint32_t builtin = 0;
    bool untyped_builtin = false;
    owned_array<uint32_t> arguments;
    /* a call through a pointer to member function: its statements
       first ([expr.mptr.oper]/6) */
    owned_array<uint32_t> through;
    bool member_pointer_call =
        node(n.a).kind == semantic_kind::member_pointer_access;
    if (member_pointer_call) {
        if (passed_indirectly(record(function_type).operand))
            return fail("a call through a pointer to member function "
                        "returning a class by address",
                        n.at);
        uint32_t object = 0;
        callee = member_function_call(n, through, object);
        if (!callee)
            return 0;
        arguments.append(object);
    } else if (n.c) {
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
        /* [ext:gnu_library_builtins] __builtin_is_constant_evaluated()
           evaluated at run time: false ([meta.const.eval]/1) */
        uint32_t named = n.a;
        if (node(named).kind == semantic_kind::conversion &&
            static_cast<conversion_kind>(node(named).op) ==
                conversion_kind::function_to_pointer)
            named = node(named).a;
        if (node(named).kind == semantic_kind::entity &&
            (analysis.entity_table[node(named).a].gnu_flags &
             gnu_is_constant_evaluated))
            return ast_builder_integer(
                c,
                AST_QUALIFIED_TYPE_UNQUALIFIED(
                    type(record(function_type).operand, n.at)),
                0);
        if (node(named).kind == semantic_kind::entity &&
            (analysis.entity_table[node(named).a].gnu_flags &
             gnu_compiler_builtin))
        /* [ext:gnu_library_builtins] no function: the C front end's
               builtin node, made below */
        {
            builtin = analysis.entity_table[node(named).a].name;
            untyped_builtin = analysis.entity_table[node(named).a].gnu_flags &
                              gnu_untyped_builtin;
        } else {
            callee = expression(n.a);
            if (!callee)
                return 0;
            if (record(function_type).kind == type_kind::pointer)
                function_type = record(function_type).operand;
            else
                callee =
                    cast(callee, ast_builder_type_pointer(c, type_of(callee)),
                         AST_CAST_FUNCTION_DECAY);
        }
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
    /* an atomic builtin's type is by its arguments: the call's */
    if (untyped_builtin)
        result = n.type;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(result, n.at));
    uint32_t count = static_cast<uint32_t>(arguments.size());
    uint32_t made = 0;
    if (builtin) {
        size_t length = 0;
        const char *spelling =
            tokens.pp.identifier_spelling(builtin - 1, &length);
        made = ast_builder_builtin_call(c, spelling, length, t,
                                        arguments.data(), count);
    } else {
        made = make(AST_NODE_CALL, t, callee,
                    ast_builder_list(c, arguments.data(), count), count, 0, 0);
    }
    if (member_pointer_call) {
        through.append(
            ast_builder_node(c, AST_NODE_EXPRESSION, t, made, 0, 0, 0, 0));
        made =
            make(AST_NODE_STATEMENT_EXPRESSION, t, block(through), 0, 0, 0, 0);
    }
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

/* [dcl.init]/17.6.1: the prvalue n initializes the object in symbol s
   (of C type t) itself: a constructor's call on it, a conditional's
   operand chosen at run time initializing it ([expr.cond]/7), else its
   value stored */
uint32_t c_lowering::initialize_into(uint32_t s, qualified_type object_type,
                                     ast_qualified_type t, uint32_t n)
{
    const semantic_node &x = node(n);
    uint32_t object = c_variable(c, s, t);
    qualified_type element;
    element_destructor(object_type, element);
    if (x.kind == semantic_kind::construct && !x.a) {
        uint64_t count = element_count(object_type, element);
        if (!count)
            return construct_at(x, object, 0, x.at);
        /* [dcl.init]/7.2: each element, first to last */
        uint32_t loop =
            each_element(s, t, type(element, x.at), count, false, x.b, x.at);
        if (!loop)
            return 0;
        return make(AST_NODE_STATEMENT_EXPRESSION,
                    ast_builder_type_basic(AST_TYPE_VOID), block(&loop, 1), 0,
                    0, 0, 0);
    }
    if (x.kind == semantic_kind::conditional && !is_glvalue(x)) {
        uint32_t cond = truth(expression(x.a));
        conditional_depth++;
        uint32_t b = initialize_into(s, object_type, t, x.b);
        uint32_t c_ = initialize_into(s, object_type, t, x.c);
        conditional_depth--;
        if (!cond || !b || !c_)
            return 0;
        ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
        return make(AST_NODE_CONDITIONAL, v, cond, cast(b, v, AST_CAST_VOID),
                    cast(c_, v, AST_CAST_VOID), 0, 0);
    }
    uint32_t value = initializer(n);
    if (!value)
        return 0;
    return make(AST_NODE_ASSIGN, AST_QUALIFIED_TYPE_UNQUALIFIED(t), object,
                value, 0, 0, 0);
}

/* [basic.lval]/7: a class prvalue where a glvalue is wanted, materialized
   in a temporary of the full-expression ([class.temporary]/2) */
uint32_t c_lowering::temporary_object(uint32_t n)
{
    const semantic_node &x = node(n);
    if (!collecting)
        return fail("a class temporary outside a statement", x.at);
    ast_qualified_type t = type(x.type, x.at);
    uint32_t held = full_expression_temporary(t);
    entity_index destructor =
        analysis.entity_table[record(x.type).entity].destructor;
    uint32_t flag = 0;
    if (destructor)
        flag = constructed_flag();
    uint32_t made = mark_constructed(initialize_into(held, x.type, t, n), flag);
    if (!made)
        return 0;
    if (destructor)
        destroyed_temporary(x, destructor, held, t, x.at, flag);
    ast_qualified_type pointer = ast_builder_type_pointer(c, t);
    uint32_t variable = c_variable(c, held, t);
    return make(AST_NODE_DEREFERENCE, t,
                make(AST_NODE_COMMA, pointer, made,
                     address_of(variable, pointer), 0, 0, 0),
                0, 0, 0, AST_NODE_FLAG_LVALUE);
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

bool c_lowering::has_mutable_subobject(qualified_type type) const
{
    qualified_type object = type;
    while (record(object).kind == type_kind::array)
        object = record(object).operand;
    if (record(object).kind != type_kind::class_type)
        return false;
    const entity &k = analysis.entity_table[record(object).entity];
    for (uint32_t i = 0; i < k.member_count; i++) {
        const entity &m =
            analysis.entity_table[analysis.class_members[k.first_member + i]];
        if (m.mutable_member || has_mutable_subobject(m.type))
            return true;
    }
    return false;
}

uint32_t c_lowering::full_expression_temporary(ast_qualified_type t)
{
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    temporaries.append(c_declare(c, temporary));
    return temporary;
}

void c_lowering::destroyed_temporary(const semantic_node &n,
                                     entity_index destructor,
                                     uint32_t temporary, ast_qualified_type t,
                                     token_index at, uint32_t flag)
{
    if (&n != extended_temporary) {
        destroyed_after(destructor, temporary, t, at, flag);
        return;
    }
    /* [class.temporary]/6: "The temporary object to which the reference
       is bound ... persists for the lifetime of the reference" */
    extended_temporary = nullptr;
    scope_record r;
    r.kind = scope_record::object;
    r.symbol = temporary;
    r.type = t;
    r.destructor = destructor;
    r.flag = flag;
    r.owner = declaring;
    live.append(r);
}

void c_lowering::destroyed_after(entity_index destructor, uint32_t temporary,
                                 ast_qualified_type t, token_index at,
                                 uint32_t flag)
{
    uint32_t object = c_variable(c, temporary, t);
    uint32_t call = special_call(
        destructor, address_of(object, ast_builder_type_pointer(c, t)), nullptr,
        0, at);
    destructions.append(if_constructed(c_statement(c, call), flag));
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
    uint32_t held = temporary_object(argument);
    if (!held)
        return 0;
    return address_of(held, ast_builder_type_pointer(c, type(parameter, at)));
}

uint32_t c_lowering::full_expression(uint32_t e, bool discarded,
                                     owned_array<uint32_t> &before,
                                     owned_array<uint32_t> &after)
{
    collection k = collect();
    uint32_t lowered = 0;
    if (discarded)
        lowered = discard(e);
    else
        lowered = initializer(e);
    collected(k, before, after);
    return lowered;
}

c_lowering::collection c_lowering::collect()
{
    collection k;
    k.outer = collecting;
    k.first = temporaries.size();
    k.first_destruction = destructions.size();
    collecting = true;
    return k;
}

void c_lowering::collected(const collection &k, owned_array<uint32_t> &before,
                           owned_array<uint32_t> &after)
{
    collecting = k.outer;
    for (size_t i = k.first; i < temporaries.size(); i++)
        before.append(temporaries[i]);
    /* [class.temporary]/5: destroyed in the reverse order of their
       construction */
    for (size_t i = destructions.size(); i-- > k.first_destruction;)
        after.append(destructions[i]);
    temporaries.resize(k.first);
    destructions.resize(k.first_destruction);
}

/* [intro.execution]/5: a full-expression where C wants an expression (a
   condition, a for statement's step): ({ temporaries; T r = e;
   destructions; r; }), a glvalue's address held instead of its value */
uint32_t c_lowering::full_value(uint32_t e, bool discarded)
{
    owned_array<uint32_t> items, after;
    uint32_t value = full_expression(e, discarded, items, after);
    if (!value || (items.empty() && after.empty()))
        return value;
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    ast_qualified_type t = type_of(value);
    if (discarded || AST_QUALIFIED_TYPE_UNQUALIFIED(t) == v) {
        items.append(c_statement(c, value));
        for (uint32_t a : after)
            items.append(a);
        return make(AST_NODE_STATEMENT_EXPRESSION, v, block(items), 0, 0, 0, 0);
    }
    bool lvalue = ast_builder_ast(c)->nodes[value].flags & AST_NODE_FLAG_LVALUE;
    ast_qualified_type held_type = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
    if (lvalue) {
        held_type = ast_builder_type_pointer(c, t);
        value = address_of(value, held_type);
    }
    uint32_t held = c_local(c, held_type);
    items.append(c_declare(c, held, value));
    for (uint32_t a : after)
        items.append(a);
    items.append(ast_builder_node(c, AST_NODE_EXPRESSION, held_type,
                                  c_variable(c, held, held_type), 0, 0, 0, 0));
    uint32_t result = make(AST_NODE_STATEMENT_EXPRESSION, held_type,
                           block(items), 0, 0, 0, 0);
    if (!lvalue)
        return result;
    return make(AST_NODE_DEREFERENCE, t, result, 0, 0, 0, AST_NODE_FLAG_LVALUE);
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
    entity_index cls = record(bound.type).entity;
    if (record(bound.type).kind == type_kind::class_type &&
        analysis.entity_table[cls].destructor) {
        /* [class.temporary]/4 and /6: a temporary with a destructor,
           destroyed by the full-expression or as the reference dies: the
           object construct() or call() made */
        uint32_t held = object(n.a);
        if (!held)
            return 0;
        return address_of(held, pointer);
    }
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
    return compound_literal_of(value, type(held.type, held.at));
}

/* (t){value}: an object of type t initialized by an initializer node */
uint32_t c_lowering::compound_literal_of(uint32_t value, ast_qualified_type t)
{
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
        /* the members C zero-fills: a pointer to data member's null value
           is not zero bits ([abi:data-member-pointers]) */
        if (holds_data_member_pointer(n.type)) {
            for (uint32_t i = 0; i < x.member_count; i++) {
                entity_index member =
                    analysis.class_members[x.first_member + i];
                if (c_fields[member] >= values.size() &&
                    holds_data_member_pointer(
                        analysis.entity_table[member].type))
                    return fail("a pointer to data member zero-initialized "
                                "as part of an object",
                                n.at);
            }
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
    if (holds_data_member_pointer(n.type) &&
        (!record(n.type).bound_known || n.count < record(n.type).bound))
        return fail("a pointer to data member zero-initialized as part of "
                    "an object",
                    n.at);
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
                        c_variable(c, s, parameter_type(x.type, n.at)), 0, 0, 0,
                        AST_NODE_FLAG_LVALUE);
        ast_qualified_type t = type(x.type, n.at);
        uint32_t v = c_variable(c, s, t);
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
        /* [expr.const]/13: an immediate invocation is its value */
        if (n.evaluation == constness::constant)
            return constant(n);
        /* a class result returned by address that initializes no object:
           materialized ([class.temporary]/2) */
        if (!at_result_object && !is_glvalue(n) && passed_indirectly(n.type))
            return temporary_object(index);
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
    case semantic_kind::member_pointer:
        return member_pointer(index);
    case semantic_kind::member_pointer_access:
        return member_pointer_object(n);
    case semantic_kind::zero:
        if (record(n.type).kind == type_kind::pointer_to_member)
            return null_member_pointer(n.type, n.at);
        if (holds_data_member_pointer(n.type))
            return fail("a zero-initialized object with a pointer to data "
                        "member in it",
                        n.at);
        return make(AST_NODE_ZERO, type(n.type, n.at), 0, 0, 0, 0, 0);
    case semantic_kind::initializer_list:
        return initializer_list(n);
    case semantic_kind::bind_reference:
        return bind_reference(n);
    case semantic_kind::construct:
        /* [class.temporary]/4: one with a destructor is destroyed at the
           end of the full-expression */
        if (!n.a && !at_result_object &&
            analysis.entity_table[record(n.type).entity].destructor)
            return temporary_object(index);
        return construct(n);
    case semantic_kind::new_object:
        return new_object(n);
    case semantic_kind::array_size:
        return array_size(n);
    case semantic_kind::delete_object:
        return delete_object(n);
    case semantic_kind::throw_exception:
        return fail("a throw-expression (no exception support)", n.at);
    case semantic_kind::type_identification:
        return fail("a typeid expression (no run-time type information)", n.at);
    case semantic_kind::array_count:
        if (!count_symbol)
            return fail("an array count outside new or delete", n.at);
        return c_variable(c, count_symbol, type(n.type, n.at));
    case semantic_kind::allocated:
        if (!allocated_symbol)
            return fail("an allocated pointer outside new or delete", n.at);
        return c_variable(c, allocated_symbol, allocated_type);
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
        uint32_t item = items[i];
        /* a labeled declaration-statement declares in the block
           ([stmt.label]/1, [stmt.dcl]): the label, then the declaration */
        while (node(item).kind == semantic_kind::identifier_label &&
               node(node(item).a).kind ==
                   semantic_kind::declaration_statement) {
            out.append(ast_builder_label_define(
                c, label(static_cast<uint32_t>(node(item).value)),
                c_statement(c, 0)));
            item = node(item).a;
        }
        const semantic_node &s = node(item);
        if (s.kind == semantic_kind::declarations) {
            block_items(s.first, s.count, out);
            continue;
        }
        if (s.kind == semantic_kind::declaration_statement) {
            declaration_items(s, out);
            continue;
        }
        out.append(statement(item));
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
    collection k = collect();
    owned_array<uint32_t> items, after;
    initialized_declaration(n, items);
    collected(k, out, after);
    for (uint32_t i : items)
        out.append(i);
    for (uint32_t a : after)
        out.append(a);
}

void c_lowering::initialized_declaration(const semantic_node &n,
                                         owned_array<uint32_t> &out)
{
    entity_index outer_declaring = declaring;
    declaring = n.a;
    initialized_variable(n, out);
    declaring = outer_declaring;
}

void c_lowering::initialized_variable(const semantic_node &n,
                                      owned_array<uint32_t> &out)
{
    entity_index e = n.a;
    const entity &x = analysis.entity_table[e];
    qualified_type object;
    entity_index destructor = element_destructor(x.type, object);
    if (n.b && x.automatic && node(n.b).kind == semantic_kind::bind_reference) {
        /* [class.temporary]/6: "The temporary object to which the
           reference is bound ... persists for the lifetime of the
           reference" (through the base subobjects it names) */
        const semantic_node *root = &node(node(n.b).a);
        while (root->kind == semantic_kind::member &&
               !is_glvalue(node(root->a)))
            root = &node(root->a);
        extended_temporary = root;
    }
    uint64_t bound = element_count(x.type, object);
    if (!x.automatic && (destructor || (n.b && !constant_initializer(n.b)))) {
        /* [stmt.dcl]/4: initialized the first time control passes
           through, under a guard */
        guarded_initialization(n, destructor, out);
        return;
    }
    if (!n.b || node(n.b).kind != semantic_kind::construct || !x.automatic) {
        /* a block's object, whose destructor is called below */
        out.append(declaration(n));
    } else {
        uint32_t s = symbol(e, n.at);
        uint32_t made = initialization(e, n.b, n.at);
        if (!made)
            return;
        out.append(c_declare(c, s));
        out.append(c_statement(c, made));
    }
    if (destructor && e != named_return) {
        scope_record r;
        r.kind = scope_record::object;
        r.symbol = symbol(e, n.at);
        r.type = type(x.type, n.at);
        r.destructor = destructor;
        r.owner = e;
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
    return block(items);
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

/*
 * [stmt.while]/2, [stmt.for]/1: while (condition) statement, with the step
 * after the statement (a for statement's expression, 0 if none):
 *     for (;;) { condition's declaration; if (condition) { statement }
 *                else { destroy the declaration's objects; break; }
 *                continue_label: step; destroy them; }
 * so that the condition's variable lives through the statement and the
 * step and is destroyed before the next test; without one, C's own loop.
 * A condition's temporaries are those of its full-expression.
 */
uint32_t c_lowering::loop(uint32_t cond_node, uint32_t step_node,
                          uint32_t body_node, token_index at)
{
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    bool declares = cond_node && node(cond_node).kind ==
                                     semantic_kind::condition_declaration;
    if (!declares) {
        uint32_t cond = 0, step = 0;
        if (cond_node) {
            cond = condition(cond_node, no_statements);
            if (!cond)
                return 0;
        }
        if (step_node) {
            step = full_value(step_node, true);
            if (!step)
                return 0;
        }
        uint32_t body = loop_body(body_node, false);
        if (!step_node && cond)
            return ast_builder_node(c, AST_NODE_WHILE, v, cond, body, 0, 0, 0);
        uint32_t step_body[2] = {step, body};
        return ast_builder_node(c, AST_NODE_FOR, v, 0, cond,
                                ast_builder_list(c, step_body, 2), 0, 0);
    }
    size_t mark = live.size();
    scope_record r;
    r.kind = scope_record::loop;
    live.append(r);
    owned_array<uint32_t> items;
    uint32_t cond = condition(cond_node, items);
    if (!cond)
        return 0;
    if (step_node) {
        live[mark].continue_label = ast_builder_label(c, "", 0);
        live[mark].continue_mark = live.size();
    }
    uint32_t body = statement(body_node);
    owned_array<uint32_t> leave;
    destroy(mark + 1, leave, at);
    leave.append(ast_builder_node(c, AST_NODE_BREAK, v, 0, 0, 0, 0, 0));
    items.append(
        ast_builder_node(c, AST_NODE_IF, v, cond, body, block(leave), 0, 0));
    if (step_node) {
        uint32_t step = full_value(step_node, true);
        if (!step)
            return 0;
        items.append(ast_builder_label_define(c, live[mark].continue_label,
                                              c_statement(c, step)));
    }
    destroy(mark + 1, items, at);
    live.resize(mark);
    uint32_t step_body[2] = {0, block(items)};
    return ast_builder_node(c, AST_NODE_FOR, v, 0, 0,
                            ast_builder_list(c, step_body, 2), 0, 0);
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
    if (x.kind == semantic_kind::entity || x.kind == semantic_kind::member ||
        x.kind == semantic_kind::member_pointer)
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
    case semantic_kind::member_pointer:
        return true;
    case semantic_kind::conversion: {
        if (address_constant(n))
            return true;
        entity_index member = 0;
        int64_t adjustment = 0;
        if (member_pointer_value(n, member, adjustment))
            return true;
        /* a read of a variable is code, unless read() folds it */
        return x.evaluation == constness::constant &&
               (static_cast<conversion_kind>(x.op) !=
                    conversion_kind::lvalue_to_rvalue ||
                folded_read(node(x.a)));
    }
    case semantic_kind::bind_reference:
        /* a reference bound to an object of static storage duration */
        return static_lvalue(x.a);
    default:
        return x.evaluation == constness::constant || address_constant(n);
    }
}

/* a variable declared in a block: an automatic one is a C declaration, a
   static one's symbol holds its initializer */
uint32_t c_lowering::declaration(const semantic_node &n)
{
    entity_index e = n.a;
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, n.at);
    if (!s)
        return 0;
    uint32_t init = 0;
    if (!x.automatic && !n.b && holds_data_member_pointer(x.type)) {
        if (record(x.type).kind != type_kind::pointer_to_member)
            return fail("a pointer to data member zero-initialized as part "
                        "of an object",
                        n.at);
        init = null_member_pointer(x.type, n.at);
    }
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
        return c_statement(c, 0);
    }
    return c_declare(c, s, init);
}

/* [stmt.pre]/4: a condition that declares is that declaration (its
   initializer a full-expression, its variable live in the statement),
   then the variable's value as the condition */
uint32_t c_lowering::condition(uint32_t n, owned_array<uint32_t> &out)
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::condition_declaration) {
        declaration_items(node(x.a), out);
        n = x.b;
    }
    uint32_t value = full_value(n, false);
    if (!value)
        return 0;
    return truth(value);
}

/* [stmt.if]/3, [stmt.switch]/7, [stmt.for]/1: an init-statement before
   the statement, its variables in the statement's scope */
void c_lowering::init_statement(uint32_t n, owned_array<uint32_t> &out)
{
    if (!n)
        return;
    const semantic_node &first = node(n);
    if (first.kind == semantic_kind::declarations)
        block_items(first.first, first.count, out);
    else if (first.kind == semantic_kind::declaration_statement)
        declaration_items(first, out);
    else if (first.kind != semantic_kind::null_statement)
        out.append(statement(n));
}

/* the automatic variables in scope at each label of the function, for a
   goto's [stmt.dcl]/2: a block's declarations before the label and those
   of the statements around it (an init-statement's, a condition's) */
void c_lowering::label_scopes(uint32_t s, owned_array<uint32_t> &path)
{
    if (!s || s >= analysis.semantics.size())
        return;
    const semantic_node &n = node(s);
    if (n.kind == semantic_kind::condition_declaration) {
        label_scopes(n.a, path);
        return;
    }
    if (n.kind < semantic_kind::block || n.kind > semantic_kind::null_statement)
        return;
    switch (n.kind) {
    case semantic_kind::declarations: {
        const uint32_t *items = analysis.semantics.items(n.first);
        for (uint32_t i = 0; i < n.count; i++)
            label_scopes(items[i], path);
        return;
    }
    case semantic_kind::declaration_statement:
        path.append(n.a);
        return;
    case semantic_kind::identifier_label:
        label_paths.append(static_cast<uint32_t>(n.value));
        label_paths.append(static_cast<uint32_t>(path.size()));
        for (uint32_t e : path)
            label_paths.append(e);
        label_scopes(n.a, path);
        return;
    default:
        break;
    }
    size_t mark = path.size();
    if (n.kind == semantic_kind::block) {
        const uint32_t *items = analysis.semantics.items(n.first);
        for (uint32_t i = 0; i < n.count; i++)
            label_scopes(items[i], path);
    } else {
        /* an if or switch's init-statement is d, before its condition a */
        label_scopes(n.d, path);
        label_scopes(n.a, path);
        label_scopes(n.b, path);
        label_scopes(n.c, path);
    }
    path.resize(mark);
}

bool c_lowering::in_label_scope(uint32_t identifier, entity_index e) const
{
    for (size_t i = 0; i + 1 < label_paths.size();) {
        uint32_t count = label_paths[i + 1];
        if (label_paths[i] == identifier) {
            for (uint32_t k = 0; k < count; k++)
                if (label_paths[i + 2 + k] == e)
                    return true;
            return false;
        }
        i += 2 + count;
    }
    return false;
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
            return c_statement(c, 0);
        uint32_t e = full_value(n.a, true);
        if (!e)
            return 0;
        return c_statement(c, e);
    }
    case semantic_kind::null_statement:
        return c_statement(c, 0);
    case semantic_kind::declaration_statement: {
        /* a substatement that is a declaration: [stmt.pre]/2 its own block
           scope */
        size_t mark = live.size();
        owned_array<uint32_t> items;
        declaration_items(n, items);
        destroy(mark, items, n.at);
        live.resize(mark);
        return block(items);
    }
    case semantic_kind::if_statement:
    case semantic_kind::switch_statement: {
        /* [stmt.if]/3, [stmt.switch]/7: an init-statement is the statement
           before the if or switch, in a block around both; a condition
           that declares likewise ([stmt.select]: its scope is the
           statement), its variables destroyed after it */
        size_t mark = live.size();
        owned_array<uint32_t> items;
        init_statement(n.d, items);
        uint32_t cond = condition(n.a, items);
        if (!cond)
            return 0;
        uint32_t made = 0;
        if (n.kind == semantic_kind::if_statement)
            made = ast_builder_node(c, AST_NODE_IF, v, cond, statement(n.b),
                                    statement(n.c), 0, 0);
        else
            made = ast_builder_node(c, AST_NODE_SWITCH, v, cond,
                                    loop_body(n.b, true), 0, 0, 0);
        if (items.empty() && live.size() == mark)
            return made;
        items.append(made);
        destroy(mark, items, n.at);
        live.resize(mark);
        return block(items);
    }
    case semantic_kind::while_statement:
        return loop(n.a, 0, n.b, n.at);
    case semantic_kind::do_statement: {
        uint32_t body = loop_body(n.a, false);
        uint32_t cond = full_value(n.b, false);
        if (!cond)
            return 0;
        return ast_builder_node(c, AST_NODE_DO, v, truth(cond), body, 0, 0, 0);
    }
    case semantic_kind::for_statement: {
        /* [stmt.for]/1: { init-statement while (condition) { statement
           expression ; } } */
        size_t mark = live.size();
        owned_array<uint32_t> items;
        init_statement(n.a, items);
        uint32_t made = loop(n.b, n.c, n.d, n.at);
        if (items.empty() && live.size() == mark)
            return made;
        items.append(made);
        destroy(mark, items, n.at);
        live.resize(mark);
        return block(items);
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
    case semantic_kind::goto_statement: {
        /* [stmt.dcl]/2: the objects live here and not at the label are
           destroyed, last constructed first; those the label has in scope
           are the first ones live (a jump into an object's scope past its
           declaration is ill-formed, [stmt.dcl]/3) */
        uint32_t identifier = static_cast<uint32_t>(n.value);
        uint32_t jump = ast_builder_node(c, AST_NODE_GOTO, v, label(identifier),
                                         0, 0, 0, 0);
        if (!leaving_objects())
            return jump;
        if (!label_paths_made) {
            owned_array<uint32_t> path;
            label_scopes(function_root, path);
            label_paths_made = true;
        }
        size_t from = live.size();
        for (size_t i = 0; i < live.size(); i++)
            if (live[i].kind == scope_record::object &&
                !in_label_scope(identifier, live[i].owner)) {
                from = i;
                break;
            }
        if (from == live.size())
            return jump;
        owned_array<uint32_t> items;
        destroy(from, items, n.at);
        items.append(jump);
        return block(items);
    }
    case semantic_kind::break_statement:
    case semantic_kind::continue_statement: {
        bool is_break = n.kind == semantic_kind::break_statement;
        int op = AST_NODE_CONTINUE;
        if (is_break)
            op = AST_NODE_BREAK;
        uint32_t jump = ast_builder_node(c, op, v, 0, 0, 0, 0, 0);
        size_t mark = leaving(is_break);
        if (!is_break && mark && live[mark - 1].continue_label) {
            /* to the step, the condition's variable still live */
            jump = ast_builder_node(c, AST_NODE_GOTO, v,
                                    live[mark - 1].continue_label, 0, 0, 0, 0);
            mark = live[mark - 1].continue_mark;
        }
        if (mark >= live.size())
            return jump;
        owned_array<uint32_t> items;
        destroy(mark, items, n.at);
        items.append(jump);
        return block(items);
    }
    case semantic_kind::return_statement: {
        uint32_t value = 0;
        owned_array<uint32_t> items, after;
        if (n.a && named_return && returned_variable(n.a) == named_return) {
            /* the variable is the result object: its bytes are the
               result, and it is not destroyed */
            uint32_t s = symbol(named_return, n.at);
            value = c_variable(
                c, s, type(analysis.entity_table[named_return].type, n.at));
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
            uint32_t held = c_local(c, t);
            items.append(c_declare(c, held, value));
            result = c_variable(c, held, t);
        } else if (value) {
            items.append(c_statement(c, value));
        }
        for (uint32_t a : after)
            items.append(a);
        destroy(0, items, n.at);
        items.append(
            ast_builder_node(c, AST_NODE_RETURN, v, result, 0, 0, 0, 0));
        return block(items);
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
        return fail("a virtual table whose symbol is not mangled", at);
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
    const dynamic_class *kd = analysis.dynamic_of(t.k);
    owned_array<vcall_entry> entries;
    vcall_functions(virtual_class, entries);
    for (const vcall_entry &e : entries) {
        const vtable_group &vg =
            analysis.vtable_groups[vd->first_group + e.group];
        int64_t own = static_cast<int64_t>(vg.offset) +
                      analysis.vtable_adjustments[vg.first_slot + e.slot];
        /* the final overrider in k's table for the virtual base's
           subobject, if k is not the virtual base itself */
        entity_index f = e.f;
        for (uint32_t h = 0; h < kd->group_count && t.k != virtual_class; h++) {
            const vtable_group &kg =
                analysis.vtable_groups[kd->first_group + h];
            if (kg.in_virtual_base == virtual_class &&
                kg.offset_in_virtual == vg.offset &&
                kg.subobject_class == vg.subobject_class)
                f = analysis.vtable_slots[kg.first_slot + e.slot];
        }
        int64_t location = 0;
        if (f == e.f) {
            out.append(own);
            continue;
        }
        if (!subobject_location(t, analysis.entity_table[f].enclosing,
                                location))
            vcall_unknown = true;
        out.append(location - at);
    }
}

void c_lowering::vcall_functions(entity_index v,
                                 owned_array<vcall_entry> &out) const
{
    const dynamic_class *vd = analysis.dynamic_of(v);
    for (uint32_t h = 0; h < vd->group_count; h++) {
        const vtable_group &vg = analysis.vtable_groups[vd->first_group + h];
        if (vg.in_virtual_base)
            continue;
        for (uint32_t k = 0; k < vg.slot_count; k++) {
            entity_index f = analysis.vtable_slots[vg.first_slot + k];
            bool repeated = false;
            for (const vcall_entry &e : out)
                repeated =
                    repeated || e.f == f || analysis.overrides_function(f, e.f);
            if (repeated)
                continue;
            vcall_entry e;
            e.f = f;
            e.group = h;
            e.slot = k;
            out.append(e);
        }
    }
}

entity_index c_lowering::virtual_slot_own(const vtable_group &g,
                                          uint32_t i) const
{
    const dynamic_class *vd = analysis.dynamic_of(g.in_virtual_base);
    for (uint32_t h = 0; vd && h < vd->group_count; h++) {
        const vtable_group &vg = analysis.vtable_groups[vd->first_group + h];
        if (!vg.in_virtual_base && vg.offset == g.offset_in_virtual &&
            vg.subobject_class == g.subobject_class)
            return analysis.vtable_slots[vg.first_slot + i];
    }
    return 0;
}

int64_t c_lowering::vcall_offset_offset(const vtable_group &g, uint32_t i) const
{
    if (!g.in_virtual_base || g.offset_in_virtual)
        return 0;
    entity_index own = virtual_slot_own(g, i);
    if (!own || own == analysis.vtable_slots[g.first_slot + i])
        return 0;
    owned_array<vcall_entry> entries;
    vcall_functions(g.in_virtual_base, entries);
    const dynamic_class *sd = analysis.dynamic_of(g.subobject_class);
    for (uint32_t j = 0; j < entries.size(); j++)
        if (entries[j].f == own ||
            analysis.overrides_function(own, entries[j].f))
            return -static_cast<int64_t>(analysis.pointer_size()) *
                   (3 + static_cast<int64_t>(sd->vbase_entry_count) + j);
    return 0;
}

/* f is t.k, one of its virtual bases, a non-virtual base of it or of
   one of its virtual bases (one such subobject) */
bool c_lowering::subobject_location(const table_spec &t, entity_index f,
                                    int64_t &out) const
{
    auto nonvirtual = [&](entity_index from, int64_t at) {
        while (from != f) {
            entity_index step = analysis.nonvirtual_step(from, f);
            at += static_cast<int64_t>(analysis.entity_table[step].offset);
            from = record(analysis.entity_table[step].type).entity;
        }
        return at;
    };
    if (f == t.k || analysis.nonvirtual_subobjects(t.k, f) == 1) {
        out = nonvirtual(t.k, 0);
        return true;
    }
    if (analysis.nonvirtual_subobjects(t.k, f))
        return false;
    uint32_t found = 0;
    const entity &x = analysis.entity_table[t.k];
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        entity_index w =
            record(
                analysis
                    .entity_table
                        [analysis.class_virtual_bases[x.first_virtual_base + i]]
                    .type)
                .entity;
        if (w != f && analysis.nonvirtual_subobjects(w, f) != 1)
            continue;
        found++;
        out = nonvirtual(
            w, static_cast<int64_t>(virtual_base_offset(t.layout, w)) -
                   static_cast<int64_t>(t.base_offset));
    }
    return found == 1;
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
    uint32_t table = c_variable(c, s, array);
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
            /* [abi:mangling-special-thunks]: an overrider outside the
               virtual base whose table this is: its virtual thunk */
            int64_t vcall = vcall_offset_offset(group, i);
            if (destructor && i && analysis.vtable_slots[slot - 1] == f) {
                ft = entity_type(f, at);
                if (!deleting_destructor(f, at, false))
                    return false;
                target = deleting_symbols[f];
                if (vcall || adjustment)
                    target = thunk(f, adjustment, at, vcall, 0);
            } else if (destructor && (vcall || adjustment)) {
                ft = entity_type(f, at);
                target = thunk(f, adjustment, at, vcall, 1);
            } else if (destructor) {
                ft = entity_type(f, at);
                target = special_callee(f, at);
            } else if (vcall && !analysis.entity_table[f].pure) {
                target = thunk(f, adjustment, at, vcall);
                ft = entity_type(f, at);
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
    if (vcall_unknown)
        return fail("a vcall offset whose final overrider's subobject is not "
                    "found once",
                    at);
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
        return fail("a VTT whose symbol is not mangled", at);
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
    /* the vbase offset: ((ptrdiff_t *)*(char **)p)[-3 - index] */
    return table_adjusted(p, again, -3 - static_cast<int64_t>(index), t);
}

/* &((char *)again)[((ptrdiff_t *)*(char **)p)[index]] as a t: p (and
   again, a second copy of it) moved by an offset in its virtual table */
uint32_t c_lowering::table_adjusted(uint32_t p, uint32_t again, int64_t index,
                                    ast_qualified_type t)
{
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = ast_builder_type_pointer(c, byte);
    ast_qualified_type ptrdiff = ast_builder_type_ptrdiff(c);
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
             ast_builder_integer(c, ptrdiff, static_cast<uint64_t>(index)), 0,
             0, AST_NODE_FLAG_LVALUE);
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
        uint32_t p = c_variable(c, vtt_parameter, vpp);
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
            uint32_t p = c_variable(c, vtt_parameter, vpp);
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

/* [conv.ptr]/3, [expr.static.cast]/11: "The null pointer value is
   converted to the null pointer value of the destination type": p ? p +
   offset : 0, p read twice; an object's address (never null, e.g. to an
   empty base the layout moved, [abi:class-types] II) without the check.
   The non-virtual base conversions n's operand is made of add their
   offsets to the one check */
uint32_t c_lowering::null_checked_offset(const semantic_node &n, int64_t offset,
                                         ast_qualified_type t)
{
    uint32_t operand = n.a;
    for (;;) {
        const semantic_node &x = node(operand);
        if (x.kind != semantic_kind::conversion)
            break;
        conversion_kind k = static_cast<conversion_kind>(x.op);
        int64_t by = static_cast<int64_t>(analysis.entity_table[x.b].offset);
        if (k == conversion_kind::base_to_derived)
            by = -by;
        else if (k != conversion_kind::derived_to_base ||
                 analysis.entity_table[x.b].virtual_base)
            break;
        offset += by;
        operand = x.a;
    }
    if (node(operand).kind == semantic_kind::address) {
        uint32_t p = expression(operand);
        if (!p)
            return 0;
        return offset_pointer(p, offset, t);
    }
    /* (e.g. a pointer to the virtual base it is in, read from the virtual
       table, is held in a temporary) */
    reread r;
    uint32_t p =
        read_first(operand, r, "a pointer to a base at a nonzero offset");
    if (!p)
        return 0;
    uint32_t again = read_again(r);
    uint32_t null =
        cast(ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0), t,
             AST_CAST_NULL_POINTER);
    return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                truth(p), offset_pointer(again, offset, t), null, 0, 0);
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
 * [abi:mangling-special-thunks]: a thunk of f.  A non-virtual one "adjusts
 * the this pointer by a constant offset" and calls f, named _ZThn <offset>
 * _ <f's encoding>; a virtual one adjusts it by the vcall offset its
 * virtual table has ([abi:vtable-components]: "the vcall offset ... is
 * used to adjust this"), named _ZTv 0 _ n<vcall offset's offset> _ <f's
 * encoding>; a destructor's for its complete object (D1) or deleting (D0)
 * variant.  It is defined where f is (with f's linkage), declared
 * elsewhere.
 */
uint32_t c_lowering::thunk(entity_index f, int64_t adjustment, token_index at,
                           int64_t vcall, int variant)
{
    for (const thunk_record &t : thunks)
        if (t.f == f && t.adjustment == adjustment && t.vcall == vcall &&
            t.variant == variant)
            return t.symbol;
    owned_buffer function_name;
    if (!itanium_symbol_name(analysis, tokens.pp, f, function_name.get(),
                             variant))
        return fail("a thunk whose function's symbol is not mangled", at);
    owned_buffer name;
    if (vcall)
        vbuffer_format_append(name.get(), "_ZTv%lld_n%llu_",
                              static_cast<long long>(adjustment),
                              static_cast<unsigned long long>(-vcall));
    else
        vbuffer_format_append(name.get(), "_ZThn%llu_",
                              static_cast<unsigned long long>(-adjustment));
    vbuffer_append(name.get(), function_name.data() + 2,
                   function_name.size() - 2);
    /* f's linkage, as symbol() gives it */
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    symbol_linkage(f, storage, flags);
    uint32_t s =
        ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name.data(), name.size(),
                           entity_type(f, at), storage, flags, 0);
    ast_builder_toplevel(c, s);
    thunk_record r;
    r.f = f;
    r.adjustment = adjustment;
    r.vcall = vcall;
    r.variant = variant;
    r.symbol = s;
    thunks.append(r);
    return s;
}

/* where virtual function f is defined: its thunks, one per adjustment
   its class's virtual table group has for it (a destructor's slot pair
   two, D1's and D0's) */
bool c_lowering::thunk_definitions(entity_index f, token_index at)
{
    const entity &x = analysis.entity_table[f];
    const dynamic_class *d = analysis.dynamic_of(x.enclosing);
    if (!d)
        return true;
    for (uint32_t g = 1; g < d->group_count; g++) {
        /* a copy: thunk() and the definitions below do not touch the
           analysis, but keep no reference across them */
        const vtable_group group = analysis.vtable_groups[d->first_group + g];
        for (uint32_t i = 0; i < group.slot_count; i++) {
            uint32_t slot = group.first_slot + i;
            int64_t adjustment = analysis.vtable_adjustments[slot];
            int64_t vcall = vcall_offset_offset(group, i);
            if (analysis.vtable_slots[slot] != f || (!adjustment && !vcall))
                continue;
            int variant = 2;
            if (x.special == special_member::destructor) {
                variant = 1;
                if (i && analysis.vtable_slots[slot - 1] == f)
                    variant = 0;
            }
            if (!variant && !deleting_destructor(f, at, false))
                return false;
            uint32_t s = thunk(f, adjustment, at, vcall, variant);
            if (!s)
                return false;
            thunk_record r;
            bool done = false;
            for (thunk_record &t : thunks)
                if (t.symbol == s) {
                    done = t.defined;
                    t.defined = true;
                    r = t;
                }
            if (!done && !thunk_body(r, at))
                return false;
        }
    }
    return true;
}

/* the thunk's definition: return f(adjusted this, arguments...), this
   adjusted by ((char *)this + adjustment) and then by the vcall offset
   *(ptrdiff_t *)(*(char **)this + vcall) */
bool c_lowering::thunk_body(const thunk_record &r, token_index at)
{
    entity_index f = r.f;
    const entity &x = analysis.entity_table[f];
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
        c, type(qualified_type::make(
                    analysis.entity_table[x.enclosing].type.unqualified(),
                    object_cv(x.member_cv)),
                at)));
    for (uint32_t k = 0; k < ft.parameter_count; k++)
        parameter_types.append(parameter_type(types[k], at));
    uint32_t count = ft.parameter_count + 1;
    owned_array<uint32_t> forwarded, arguments;
    for (uint32_t k = 0; k < count; k++) {
        ast_qualified_type t = parameter_types[k];
        uint32_t p = c_local(c, t);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        forwarded.append(p);
        auto read = [&] { return c_variable(c, p, t); };
        uint32_t value = read();
        if (!k && r.adjustment)
            value = offset_pointer(value, r.adjustment, t);
        if (!k && r.vcall)
            /* the vcall offset at r.vcall bytes from the address point */
            value = table_adjusted(
                read(), value,
                r.vcall / static_cast<int64_t>(analysis.pointer_size()), t);
        arguments.append(value);
    }
    uint32_t target = symbol(f, at, r.variant);
    if (!target)
        return false;
    ast_builder_symbol_flags_add(c, target, AST_SYMBOL_FLAG_USED);
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, function, target, 0, 0, 0, 0),
             ast_builder_type_pointer(c, function), AST_CAST_FUNCTION_DECAY);
    ast_qualified_type result = type(ft.operand, at);
    uint32_t call =
        make(AST_NODE_CALL, result, callee,
             ast_builder_list(c, arguments.data(), count), count, 0, 0);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    uint32_t statement_node = 0;
    if (record(ft.operand).kind == type_kind::fundamental &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(result) == v)
        statement_node = c_statement(c, call);
    else
        statement_node =
            ast_builder_node(c, AST_NODE_RETURN, v, call, 0, 0, 0, 0);
    uint32_t body = block(&statement_node, 1);
    struct node &b = ast_builder_ast(c)->nodes[body];
    b.a = ast_builder_list(c, forwarded.data(), count);
    b.aux = static_cast<uint16_t>(count);
    ast_builder_symbol_body(c, r.symbol, body);
    ast_builder_symbol_flags_add(c, r.symbol, AST_SYMBOL_FLAG_DEFINED);
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
    uint32_t p = c_local(c, self);
    ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
    auto self_value = [&] { return c_variable(c, p, self); };
    uint32_t statements[2];
    statements[0] = special_call(e, self_value(), nullptr, 0, at);
    if (!statements[0])
        return false;
    statements[0] = c_statement(c, statements[0]);
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
    statements[1] = c_statement(c, call);
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
    uint32_t call = special_call(e, c_variable(c, self, pointer), nullptr, 0,
                                 body.at, element_address(table, array, 0));
    if (!call)
        return false;
    labels.clear();
    live.clear();
    in_function = true;
    structor_variant = 3;
    structor_class = k;
    structor_this = items[0];
    uint32_t statements[2] = {c_statement(c, call), statement(body.c)};
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
    label_paths.clear();
    label_paths_made = false;
    function_root = body.a;
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
            p = c_local(c, parameter_type(types[i - implicit], body.at));
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
        vtt_parameter = c_local(
            c, ast_builder_type_pointer(c, ast_builder_type_pointer(c, v)));
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
        uint32_t p = c_local(c, t);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        forwarded.append(p);
        arguments.append(c_variable(c, p, t));
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
    uint32_t statement_node = c_statement(c, call);
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
    if (!static_zero_initialization(x, s, 0))
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
            ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
            return global_initializer(e, false);
        }
        uint32_t init = expression(x.definition);
        if (!init)
            return false;
        ast_builder_symbol_body(c, s, init);
        if (destructor) {
            /* [basic.start.term]/1: constant-initialized, its destructor
               registered at start-up */
            if (!global_initializer(e, true))
                return false;
        }
    } else {
        qualified_type object;
        if (element_destructor(x.type, object)) {
            /* [basic.start.term]/1: zero-initialized (no non-trivial
               default constructor), its destructor registered */
            if (!global_initializer(e, true))
                return false;
        }
    }
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

/*
 * [basic.start.static]/2: "If constant initialization is not performed, a
 * variable with static storage duration ... is zero-initialized", which
 * for "a scalar type" is "the value obtained by converting the integer
 * literal 0 to T" ([dcl.init]/6.1): the null member pointer value, which
 * [abi:data-member-pointers] represents as -1, not C's zero bits.  A
 * pointer to data member without an initializer gets it; one inside a
 * class or array that is not constant-initialized is not lowered.
 */
bool c_lowering::static_zero_initialization(const entity &x, uint32_t s,
                                            token_index at)
{
    if (!holds_data_member_pointer(x.type))
        return true;
    if (x.definition && constant_initializer(x.definition))
        return true;
    if (!x.definition && record(x.type).kind == type_kind::pointer_to_member) {
        ast_builder_symbol_body(c, s, null_member_pointer(x.type, at));
        return true;
    }
    fail("a pointer to data member zero-initialized as part of an object", at);
    return false;
}

/* a function void name(void) of internal linkage, named as written (its
   body to come) */
uint32_t c_lowering::generated_function(const char *base)
{
    bool used = false;
    for (const char *n : generated_names)
        used = used || !strcmp(n, base);
    owned_buffer name;
    vbuffer_string_append(name.get(), base);
    if (used)
        vbuffer_format_append(name.get(), ".%u", ++generated_suffix);
    else
        generated_names.append(base);
    return local_function(name.data(), name.size());
}

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
    uint32_t s = symbol(e, at);
    if (!s)
        return 0;
    qualified_type t = analysis.entity_table[e].type;
    return initialize_into(s, t, type(t, at), init);
}

/* [intro.execution]/5.3: the initialization of a static variable, a
   full-expression whose temporaries are made and destroyed with it */
void c_lowering::static_initialization(entity_index e, uint32_t init,
                                       owned_array<uint32_t> &out,
                                       token_index at)
{
    collection k = collect();
    owned_array<uint32_t> after;
    uint32_t done = initialization(e, init, at);
    collected(k, out, after);
    if (!done)
        return;
    out.append(c_statement(c, done));
    for (uint32_t a : after)
        out.append(a);
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
void c_lowering::static_destruction(entity_index e, owned_array<uint32_t> &out,
                                    token_index at)
{
    const entity &x = analysis.entity_table[e];
    qualified_type object;
    entity_index destructor = element_destructor(x.type, object);
    if (!destructor)
        return;
    uint32_t s = symbol(e, at);
    if (!s)
        return;
    ast_qualified_type t = type(x.type, at);
    uint64_t count = element_count(x.type, object);
    uint32_t registered = 0;
    if (!count) {
        registered = destructor_registration(
            special_callee(destructor, at), entity_type(destructor, at),
            address_of(c_variable(c, s, t), ast_builder_type_pointer(c, t)));
    } else {
        /* [class.dtor]/13, [impl.clang]: __cxx_global_array_dtor (then
           .1, ...), destroying the elements in reverse order */
        uint32_t f = generated_function("__cxx_global_array_dtor");
        uint32_t loop =
            each_element(s, t, type(object, at), count, true, destructor, at);
        if (!loop)
            return;
        ast_builder_symbol_body(c, f, block(&loop, 1));
        ast_builder_symbol_flags_add(c, f, AST_SYMBOL_FLAG_DEFINED);
        ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
        registered = destructor_registration(
            f, ast_builder_type_function(c, v, nullptr, 0, 0),
            cast(ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_LONG),
                                     0),
                 ast_builder_type_pointer(c, v), AST_CAST_INTEGER_TO_POINTER));
    }
    if (registered)
        out.append(c_statement(c, registered));
}

uint32_t c_lowering::destructor_registration(uint32_t d, ast_qualified_type f,
                                             uint32_t object)
{
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
    uint32_t function =
        cast(make(AST_NODE_VARIABLE, f, d, 0, 0, 0, 0),
             ast_builder_type_pointer(c, f), AST_CAST_FUNCTION_DECAY);
    uint32_t handle = address_of(c_variable(c, dso_handle, byte),
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
        fail("an entity whose symbol is not mangled", n.at);
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
        return address_of(c_variable(c, guard, i64), i64p);
    };
    owned_array<uint32_t> inner;
    if (!constant)
        static_initialization(e, n.b, inner, n.at);
    if (destructor)
        static_destruction(e, inner, n.at);
    if (failure.what)
        return;
    uint32_t argument = guard_address();
    uint32_t release = runtime_call(guard_release, "__cxa_guard_release", v,
                                    &i64p, &argument, 1);
    inner.append(c_statement(c, release));
    argument = guard_address();
    uint32_t acquire = runtime_call(guard_acquire, "__cxa_guard_acquire",
                                    int_type, &i64p, &argument, 1);
    uint32_t acquired =
        ast_builder_node(c, AST_NODE_IF, v,
                         make(AST_NODE_BINARY, int_type, acquire,
                              ast_builder_integer(c, int_type, 0), 0,
                              LEX_TOKEN_EXCLAMATION_EQUAL, 0),
                         block(inner), 0, 0, 0);
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
/* the function initializing the static variable e (or, only_destroy,
   registering its destruction), its call appended to initializers */
bool c_lowering::global_initializer(entity_index e, bool only_destroy)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &init = node(x.definition);
    if (!only_destroy && init.kind == semantic_kind::initializer_list)
        /* an aggregate's elements, some not constant: each stored */
        return fail("dynamic initialization of an aggregate", init.at);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    uint32_t f = generated_function("__cxx_global_var_init");
    in_function = true;
    labels.clear();
    live.clear();
    owned_array<uint32_t> statements;
    if (!only_destroy)
        static_initialization(e, x.definition, statements, init.at);
    /* [basic.start.term]/3: registered with __cxa_atexit once
       constructed ([abi:dso-dtor]) */
    if (!failure.what)
        static_destruction(e, statements, init.at);
    in_function = false;
    if (failure.what || statements.empty())
        return false;
    ast_builder_symbol_body(c, f, block(statements));
    ast_builder_symbol_flags_add(
        c, f, AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_USED);
    ast_qualified_type ft = ast_builder_type_function(c, v, nullptr, 0, 0);
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, ft, f, 0, 0, 0, 0),
             ast_builder_type_pointer(c, ft), AST_CAST_FUNCTION_DECAY);
    initializers.append(
        c_statement(c, make(AST_NODE_CALL, v, callee, 0, 0, 0, 0)));
    return true;
}

bool c_lowering::dynamic_initialization()
{
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
    ast_builder_symbol_body(c, start, block(initializers));
    ast_builder_symbol_flags_add(c, start, AST_SYMBOL_FLAG_DEFINED);
    ast_builder_symbol_constructor(c, start);
    return true;
}

bool c_lowering::translation_unit()
{
    if (!cplusplus::analysis_complete(analysis, failure))
        return false;
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
        /* a function with a placeholder return type never defined: its
           name is in no expression ([dcl.spec.auto]/11), so it needs no
           symbol (clang emits none) */
        if (x.kind == entity_kind::function && x.placeholder_return &&
            !x.defined)
            continue;
        /* a deleted function: no symbol ([dcl.fct.def.delete]) */
        if (x.kind == entity_kind::function && x.deleted)
            continue;
        /* a function declared, not defined, whose symbol is not mangled
           yet (a specialization of a template whose signature names a
           dependent decltype): a use of it fails where it is.
           [impl.clang] a specialization for a closure unique to the
           translation unit: its symbol made at its first use, which
           numbers the closure (itanium_mangler) */
        if ((x.kind == entity_kind::function && !x.defined) ||
            x.instantiation) {
            owned_buffer probe;
            analysis.number_closures = false;
            bool mangled = itanium_symbol_name(analysis, tokens.pp, d.entity,
                                               probe.get(), 0);
            analysis.number_closures = true;
            if (!mangled)
                continue;
        }
        declaring_symbols = true;
        bool made = symbol(d.entity, 0);
        declaring_symbols = false;
        if (!made)
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
    if (!initializers.empty() && !dynamic_initialization())
        return false;
    /* [abi:vague-vtable]: the virtual table of each class whose key
       function this translation unit defines */
    for (entity_index k = 1; k < analysis.entity_table.size(); k++) {
        const dynamic_class *d = analysis.dynamic_of(k);
        if (d && d->key_function &&
            analysis.entity_table[d->key_function].defined && !vtable(k, 0))
            return false;
    }
    /* the member functions defined, in the order of their definitions,
       and the functions friend declarations define ([class.friend]/6) */
    for (const semantic_analysis::definition_record &d : analysis.definitions) {
        const entity &x = analysis.entity_table[d.entity];
        bool friend_ = analysis.defined_by_friend(d.entity);
        if (!x.defined && !friend_)
            continue;
        if (x.kind != entity_kind::function ||
            (!friend_ &&
             (!x.enclosing || analysis.entity_table[x.enclosing].kind !=
                                  entity_kind::class_type)))
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

bool analysis_complete(const semantic_analysis &analysis, lowering_failure &why)
{
    for (const analysed_declaration &d : analysis.declarations)
        if (d.result != analysis_result::well_formed) {
            why.at = d.node.first_token();
            if (d.result == analysis_result::unsupported && d.has_unsupported) {
                const unsupported_construct &u =
                    analysis.unsupported[d.first_unsupported];
                why.subclause = u.subclause;
                why.file = u.file;
                why.line = u.line;
                why.function = u.function;
                why.at = u.node.first_token();
            }
            why.what = "a declaration that is not well-formed";
            why.front_end = true;
            if (d.result == analysis_result::not_parsed) {
                why.what = "a declaration the parser kept as tokens";
                /* the first part of it the parser kept, and why */
                auto node = d.node;
                for (const parser::unparsed_reason &r :
                     analysis.parsed().unparsed_reasons)
                    if (r.first >= node.first_token() &&
                        r.first < node.end_token() && r.expected) {
                        why.parse_at = r.at;
                        why.expected = r.expected;
                        why.parse_subclause = r.subclause;
                        why.later_step = r.later_step;
                        break;
                    }
            }
            return false;
        }
    return true;
}

bool lower_translation_unit(const semantic_analysis &analysis,
                            token_stream &tokens, cc *c, int target,
                            lowering_failure &why, const char *main_path)
{
    c_lowering lowering(analysis, tokens, c, target, main_path);
    /* [impl.clang]: the closures unique to the translation unit numbered
       in the order their symbols are made (itanium_mangler) */
    analysis.unnumbered_closures.clear();
    analysis.number_closures = true;
    bool lowered = lowering.translation_unit();
    why = lowering.failure;
    return lowered;
}

} // namespace cplusplus
