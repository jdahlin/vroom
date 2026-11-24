/*
 * allocation-functions.cpp: [basic.stc.dynamic] Dynamic storage duration: the
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
 * new-extended alignment here (new-expression.cpp refuses one).  The sized
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
        declare_implicit_allocation_function(f.kind, result, parameters, count);
    }
}

/* one implicit declaration, appended to the global overloads of its name */
void semantic_analysis::declare_implicit_allocation_function(
    allocation_function kind, qualified_type result,
    const qualified_type *parameters, uint32_t count)
{
    bool allocation = kind == allocation_function::new_ ||
                      kind == allocation_function::new_array;
    uint32_t name = allocation_function_name(kind);
    entity_index e = entity_table.make(entity_kind::function, name, 0);
    entity &x = entity_table[e];
    /* [basic.stc.dynamic]/2: the deallocation functions are declared
       noexcept, part of their type since C++17 */
    bool non_throwing =
        !allocation && tokens.files.behavior.has(
                           language_feature::exception_specification_in_type);
    x.type = qualified_type::make(
        type_table.function(result, parameters, count, false, non_throwing),
        cv_none);
    x.linkage_of_name = linkage::external;
    x.operator_function = true;
    x.allocation = kind;
    x.implicit = true;
    /* [except.spec]/12: a deallocation function with no explicit
       noexcept-specifier has a non-throwing exception specification; an
       allocation function's (C++03's throw(std::bad_alloc)) is left
       unrecorded */
    if (!allocation)
        x.exceptions.allows = exception_allowance::none;
    entity_index first = entity_table.find(0, name);
    if (!first) {
        entity_table.bind(0, name, e);
        return;
    }
    entity_index last = first;
    for (entity_index k = first; k; k = entity_table[k].next_overload) {
        last = k;
        if (k != e && entity_table[k].type == x.type)
            /* the program declared it already: the same function */
            return;
    }
    entity_table[last].next_overload = e;
}

/*
 * The forms with std::align_val_t ([new.delete.single]/1, the declarations
 * of <new>), which clang also declares implicitly for its __builtin_operator_new
 * and __builtin_operator_delete when aligned allocation is on, before <new>
 * is included: declared when std::align_val_t has been, once.  The sized
 * form follows the other deallocation functions.
 */
void semantic_analysis::implicit_aligned_allocation_functions()
{
    if (aligned_allocation_declared)
        return;
    entity_index std_ =
        entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
    if (!std_ || !is_namespace(std_) || !std_namespace(std_))
        return;
    lookup_result found = lookup_in_namespace(
        std_, tokens.pp.identifier("align_val_t", 11) + 1, lookup_filter::any);
    if (found.ambiguous || !found.entity ||
        entity_table[found.entity].kind != entity_kind::enumeration)
        return;
    aligned_allocation_declared = true;
    qualified_type align = qualified_type::make(
        entity_table[found.entity].type.unqualified(), cv_none);
    qualified_type size = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    qualified_type v = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    qualified_type pointer =
        qualified_type::make(type_table.pointer_to(v), cv_none);
    qualified_type new_parameters[2] = {size, align};
    declare_implicit_allocation_function(allocation_function::new_, pointer,
                                         new_parameters, 2);
    qualified_type delete_parameters[3] = {pointer, align, align};
    declare_implicit_allocation_function(allocation_function::delete_, v,
                                         delete_parameters, 2);
    if (tokens.files.behavior.has(language_feature::sized_deallocation)) {
        delete_parameters[1] = size;
        delete_parameters[2] = align;
        declare_implicit_allocation_function(allocation_function::delete_, v,
                                             delete_parameters, 3);
    }
}

/* std::align_val_t, when <new> declared it (and the aligned forms with it) */
bool semantic_analysis::align_val_type(qualified_type &type)
{
    implicit_aligned_allocation_functions();
    if (!aligned_allocation_declared)
        return false;
    entity_index std_ =
        entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
    lookup_result found = lookup_in_namespace(
        std_, tokens.pp.identifier("align_val_t", 11) + 1, lookup_filter::any);
    type = qualified_type::make(entity_table[found.entity].type.unqualified(),
                                cv_none);
    return true;
}

/*
 * [ext:gnu_library_builtins] clang's __builtin_operator_new and
 * __builtin_operator_delete (libc++'s <__new/allocate.h>): a call of the
 * replaceable global allocation or deallocation function, found as
 * ::operator new or ::operator delete is ([basic.stc.dynamic]/1, [expr.new]/9
 * for the forms the program declares, such as std::align_val_t's); a class's
 * own operator is not considered.  The name of any other spelling is 0.
 */
entity_index semantic_analysis::allocation_builtin(uint32_t name)
{
    if (!tokens.files.behavior.has(extension::gnu_library_builtins))
        return 0;
    size_t length = 0;
    const char *spelling = tokens.pp.identifier_spelling(name - 1, &length);
    allocation_function kind;
    if (length == 22 && !memcmp(spelling, "__builtin_operator_new", 22))
        kind = allocation_function::new_;
    else if (length == 25 && !memcmp(spelling, "__builtin_operator_delete", 25))
        kind = allocation_function::delete_;
    else
        return 0;
    implicit_allocation_functions();
    implicit_aligned_allocation_functions();
    return entity_table.find(0, allocation_function_name(kind));
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
semantic_analysis::allocation_function_declaration(SyntaxRef n, entity_index f)
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
    /* (clang: one that is no member is "just a placement delete") */
    if (member && function.parameter_count >= 2 &&
        destroying_delete_tag(parameters[1])) {
        /* [basic.stc.dynamic.deallocation]/2: "A destroying operator
           delete shall be a class member function named operator
           delete."; /3: "If the function is a destroying operator delete
           declared in class type C, the type of its first parameter shall
           be C*" */
        const struct type &first = type_table[parameters[0].unqualified()];
        if (first.kind != type_kind::pointer || parameters[0].cv() ||
            first.operand.cv() ||
            type_table[first.operand.unqualified()].kind !=
                type_kind::class_type ||
            type_table[first.operand.unqualified()].entity != x.enclosing)
            return ill_formed(n,
                              "the first parameter of a destroying "
                              "operator delete shall be C*",
                              subclause);
        if (function.parameter_count != 2 || x.default_arguments ||
            x.unknown_defaults || function.variadic)
            return not_supported(n, subclause);
        allocation_declarations_analysed++;
        return analysis_result::well_formed;
    }
    if (!void_pointer)
        return not_supported(n, subclause);
    if (x.default_arguments || x.unknown_defaults)
        return not_supported(n, subclause);
    allocation_declarations_analysed++;
    return analysis_result::well_formed;
}

/* whether t is std::destroying_delete_t ([support.types.byteops] <new>) */
bool semantic_analysis::destroying_delete_tag(qualified_type t) const
{
    const struct type &c = type_table[t.unqualified()];
    if (c.kind != type_kind::class_type)
        return false;
    entity_index std_ =
        entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
    if (!std_ || !is_namespace(std_) || !std_namespace(std_))
        return false;
    lookup_result found = lookup_in_namespace(
        std_, tokens.pp.identifier("destroying_delete_t", 19) + 1,
        lookup_filter::any);
    return !found.ambiguous && found.entity && found.entity == c.entity;
}

/*
 * [new.delete.placement]/1: whether f is the reserved non-allocating form
 * ::operator new(std::size_t, void *) (or the array form's), declared in
 * the global namespace; clang's isReservedGlobalPlacementOperator.
 */
bool semantic_analysis::reserved_placement(entity_index f) const
{
    const entity &x = entity_table[f];
    if (x.enclosing || x.kind != entity_kind::function)
        return false;
    const struct type &function = type_table[x.type.unqualified()];
    if (function.parameter_count != 2)
        return false;
    qualified_type second = type_table.parameters(x.type.unqualified())[1];
    const struct type &pointer = type_table[second.unqualified()];
    return pointer.kind == type_kind::pointer && !second.cv() &&
           fundamental_of(pointer.operand) == fundamental_type::void_ &&
           !pointer.operand.cv();
}

} // namespace cplusplus
