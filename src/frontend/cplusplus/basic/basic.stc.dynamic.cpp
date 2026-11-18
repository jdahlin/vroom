/*
 * basic.stc.dynamic.cpp: [basic.stc.dynamic] Dynamic storage duration: the
 * allocation and deallocation functions, their declarations' rules and the
 * global ones every translation unit has.
 *
 * [basic.stc.dynamic]/1: "A C++ implementation provides access to, and
 * management of, dynamic storage via the global allocation functions
 * operator new and operator new[] and the global deallocation functions
 * operator delete and operator delete[]."
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [basic.stc.dynamic]/2: "The following allocation and deallocation
 * functions ([support.dynamic]) are implicitly declared in global scope in
 * each translation unit of a program.
 *
 *     [[nodiscard]] void* operator new(std::size_t);
 *     void operator delete(void*) noexcept;
 *     void operator delete(void*, std::size_t) noexcept;
 *     ..."
 *
 * The forms with std::align_val_t are not declared: no type has
 * new-extended alignment here (expr.new.cpp refuses one).  The sized
 * operator delete is C++14's (N3778).  "These implicit declarations
 * introduce only the function names operator new, operator new[], operator
 * delete, and operator delete[]."
 */
void semantic_analysis::implicit_allocation_functions()
{
    if (allocation_functions_declared)
        return;
    allocation_functions_declared = true;
    qualified_type size = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    qualified_type v = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    qualified_type pointer =
        qualified_type::make(type_table.pointer_to(v), cv_none);
    bool sized =
        tokens.files.behavior.has(language_feature::sized_deallocation);
    struct form {
        allocation_function kind;
        bool sized;
    };
    const form forms[] = {
        {allocation_function::new_, false},
        {allocation_function::new_array, false},
        {allocation_function::delete_, false},
        {allocation_function::delete_, true},
        {allocation_function::delete_array, false},
        {allocation_function::delete_array, true},
    };
    for (const form &f : forms) {
        if (f.sized && !sized)
            continue;
        bool allocation = f.kind == allocation_function::new_ ||
                          f.kind == allocation_function::new_array;
        qualified_type parameters[2] = {pointer, size};
        qualified_type result = v;
        uint32_t count = 1;
        if (allocation) {
            parameters[0] = size;
            result = pointer;
        } else if (f.sized) {
            count = 2;
        }
        uint32_t name = allocation_function_name(f.kind);
        entity_index e = entity_table.make(entity_kind::function, name, 0);
        entity &x = entity_table[e];
        /* [basic.stc.dynamic]/2: the deallocation functions are declared
           noexcept, part of their type since C++17 */
        bool non_throwing =
            !allocation &&
            tokens.files.behavior.has(
                language_feature::exception_specification_in_type);
        x.type = qualified_type::make(
            type_table.function(result, parameters, count, false, non_throwing),
            cv_none);
        x.linkage_of_name = linkage::external;
        x.operator_function = true;
        x.allocation = f.kind;
        x.implicit = true;
        entity_index first = entity_table.find(0, name);
        if (!first) {
            entity_table.bind(0, name, e);
            continue;
        }
        entity_index last = first;
        while (entity_table[last].next_overload)
            last = entity_table[last].next_overload;
        entity_table[last].next_overload = e;
    }
}

/* the operator new and operator delete tokens of the translation unit:
   each a declaration's name (or a call's, counted too: a smaller count
   would conclude more) */
uint32_t semantic_analysis::allocation_names_spelled()
{
    uint32_t count = 0;
    size_t end = tokens.made();
    for (size_t i = 0; i + 1 < end; i++) {
        const token &t = tokens[i];
        if (t.kind != token_kind::keyword ||
            p.keyword_of(t) != standard_keyword::operator_)
            continue;
        const token &next = tokens[i + 1];
        if (next.kind == token_kind::keyword &&
            (p.keyword_of(next) == standard_keyword::new_ ||
             p.keyword_of(next) == standard_keyword::delete_))
            count++;
    }
    return count;
}

/*
 * [basic.stc.dynamic.allocation]/1: "An allocation function shall be a
 * class member function or a global function; a program is ill-formed if an
 * allocation function is declared in a namespace scope other than global
 * scope or declared static in global scope.  The return type shall be
 * void*.  The first parameter shall have type std::size_t
 * ([support.types]).  The first parameter shall not have an associated
 * default argument ([dcl.fct.default])."
 *
 * [basic.stc.dynamic.deallocation]/1: "Deallocation functions shall be class
 * member functions or global functions; a program is ill-formed if
 * deallocation functions are declared in a namespace scope other than global
 * scope or declared static in global scope."  /3: "Each deallocation
 * function shall return void. ... the type of its first parameter shall be
 * void*."  A destroying operator delete (/2) is not analysed.
 *
 * [class.free]/1: "Any allocation function for a class T is a static member
 * (even if not explicitly declared static)", and /3 the same of a
 * deallocation function.
 */
analysis_result
semantic_analysis::allocation_function_declaration(node_index n, entity_index f)
{
    entity &x = entity_table[f];
    bool allocation = x.allocation == allocation_function::new_ ||
                      x.allocation == allocation_function::new_array;
    const char *subclause = "basic.stc.dynamic.deallocation";
    if (allocation)
        subclause = "basic.stc.dynamic.allocation";
    bool member = x.enclosing &&
                  entity_table[x.enclosing].kind == entity_kind::class_type;
    if (member) {
        x.nonstatic_member = false;
        x.member_cv = 0;
    } else if (x.enclosing) {
        return ill_formed(n,
                          "an allocation or deallocation function shall be a "
                          "class member function or a global function",
                          subclause);
    } else if (x.linkage_of_name == linkage::internal &&
               !x.unnamed_type_linkage) {
        /* (internal linkage by the static specifier: one by a parameter
           of a type of an unnamed namespace is [impl.clang]'s, no
           declaration static) */
        return ill_formed(n,
                          "an allocation or deallocation function shall not "
                          "be declared static in global scope",
                          subclause);
    }
    /* (interned before the references into type_table below are taken) */
    qualified_type size = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    const struct type &function = type_table[x.type.unqualified()];
    const qualified_type *parameters =
        type_table.parameters(x.type.unqualified());
    const struct type &result = type_table[function.operand.unqualified()];
    if (allocation) {
        bool returns_pointer =
            result.kind == type_kind::pointer && !function.operand.cv() &&
            fundamental_of(result.operand) == fundamental_type::void_ &&
            !result.operand.cv();
        if (!returns_pointer)
            return ill_formed(n,
                              "the return type of an allocation function "
                              "shall be void*",
                              subclause);
        /* a first parameter of class type: C++26's type-aware allocation
           functions (std::type_identity<T>, P2719), which C++20 has no
           rules for */
        if (function.parameter_count &&
            (type_table[parameters[0].unqualified()].kind ==
                 type_kind::class_type ||
             type_table[parameters[0].unqualified()].kind ==
                 type_kind::dependent))
            return not_supported(n, subclause);
        if (!function.parameter_count || !(parameters[0] == size))
            return ill_formed(n,
                              "the first parameter of an allocation function "
                              "shall have type std::size_t",
                              subclause);
        if (x.default_arguments || x.unknown_defaults)
            /* the first parameter's default argument is ill-formed, the
               others' are not analysed */
            return not_supported(n, subclause);
        allocation_declarations_analysed++;
        return analysis_result::well_formed;
    }
    if (result.kind != type_kind::fundamental ||
        result.fundamental != fundamental_type::void_ || function.operand.cv())
        return ill_formed(n, "each deallocation function shall return void",
                          subclause);
    bool void_pointer = false;
    if (function.parameter_count) {
        const struct type &first = type_table[parameters[0].unqualified()];
        void_pointer =
            first.kind == type_kind::pointer && !parameters[0].cv() &&
            fundamental_of(first.operand) == fundamental_type::void_ &&
            !first.operand.cv();
    }
    if (!void_pointer)
        /* a destroying operator delete's first parameter is C* (/3) */
        return not_supported(n, subclause);
    if (x.default_arguments || x.unknown_defaults)
        return not_supported(n, subclause);
    allocation_declarations_analysed++;
    return analysis_result::well_formed;
}

} // namespace cplusplus
