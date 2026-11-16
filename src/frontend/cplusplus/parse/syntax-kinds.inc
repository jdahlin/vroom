/*
 * The kinds of syntax tree node: the grammar's nonterminals, by the name
 * the standard gives them and the subclause whose grammar defines them
 * (docs/cxx-spec/grammar-index.md).  A production that only passes its
 * operand on makes no node (doc/PLAN-cxx-parse.md).
 *
 * SYNTAX_NODE(name, nonterminal, subclause)
 */
SYNTAX_NODE(translation_unit, "translation-unit", "basic.link")
/* [dcl.pre] */
SYNTAX_NODE(declaration_seq, "declaration-seq", "dcl.pre")
SYNTAX_NODE(simple_declaration, "simple-declaration", "dcl.pre")
SYNTAX_NODE(empty_declaration, "empty-declaration", "dcl.pre")
SYNTAX_NODE(static_assert_declaration, "static_assert-declaration", "dcl.pre")
SYNTAX_NODE(alias_declaration, "alias-declaration", "dcl.pre")
/* [dcl.spec], [dcl.type] */
SYNTAX_NODE(decl_specifier_seq, "decl-specifier-seq", "dcl.spec")
SYNTAX_NODE(type_specifier_seq, "type-specifier-seq", "dcl.type")
SYNTAX_NODE(defining_type_specifier_seq, "defining-type-specifier-seq", "dcl.type")
SYNTAX_NODE(simple_type_specifier, "simple-type-specifier", "dcl.type.simple")
SYNTAX_NODE(elaborated_type_specifier, "elaborated-type-specifier", "dcl.type.elab")
SYNTAX_NODE(decltype_specifier, "decltype-specifier", "dcl.type.decltype")
SYNTAX_NODE(placeholder_type_specifier, "placeholder-type-specifier", "dcl.spec.auto")
SYNTAX_NODE(class_specifier, "class-specifier", "class.pre")
SYNTAX_NODE(enum_specifier, "enum-specifier", "dcl.enum")
SYNTAX_NODE(enum_head, "enum-head", "dcl.enum")
SYNTAX_NODE(enum_base, "enum-base", "dcl.enum")
/* [expr.prim.id] */
SYNTAX_NODE(nested_name_specifier, "nested-name-specifier", "expr.prim.id.qual")
SYNTAX_NODE(qualified_id, "qualified-id", "expr.prim.id.qual")
SYNTAX_NODE(unqualified_id, "unqualified-id", "expr.prim.id.unqual")
/* [dcl.decl], [dcl.name], [dcl.fct] */
SYNTAX_NODE(init_declarator_list, "init-declarator-list", "dcl.decl")
SYNTAX_NODE(init_declarator, "init-declarator", "dcl.decl")
SYNTAX_NODE(declarator, "declarator", "dcl.decl")
SYNTAX_NODE(ptr_declarator, "ptr-declarator", "dcl.decl")
SYNTAX_NODE(noptr_declarator, "noptr-declarator", "dcl.decl")
SYNTAX_NODE(declarator_id, "declarator-id", "dcl.decl")
SYNTAX_NODE(ptr_operator, "ptr-operator", "dcl.decl")
SYNTAX_NODE(cv_qualifier_seq, "cv-qualifier-seq", "dcl.decl")
SYNTAX_NODE(parameters_and_qualifiers, "parameters-and-qualifiers", "dcl.decl")
SYNTAX_NODE(trailing_return_type, "trailing-return-type", "dcl.decl")
SYNTAX_NODE(type_id, "type-id", "dcl.name")
SYNTAX_NODE(defining_type_id, "defining-type-id", "dcl.name")
SYNTAX_NODE(abstract_declarator, "abstract-declarator", "dcl.name")
SYNTAX_NODE(ptr_abstract_declarator, "ptr-abstract-declarator", "dcl.name")
SYNTAX_NODE(noptr_abstract_declarator, "noptr-abstract-declarator", "dcl.name")
SYNTAX_NODE(parameter_declaration_clause, "parameter-declaration-clause", "dcl.fct")
SYNTAX_NODE(parameter_declaration_list, "parameter-declaration-list", "dcl.fct")
SYNTAX_NODE(parameter_declaration, "parameter-declaration", "dcl.fct")
SYNTAX_NODE(noexcept_specifier, "noexcept-specifier", "except.spec")
/* [dcl.init], [dcl.fct.def.general] */
SYNTAX_NODE(initializer, "initializer", "dcl.init")
SYNTAX_NODE(brace_or_equal_initializer, "brace-or-equal-initializer", "dcl.init")
SYNTAX_NODE(function_definition, "function-definition", "dcl.fct.def.general")
SYNTAX_NODE(function_body, "function-body", "dcl.fct.def.general")
/* [basic.namespace], [dcl.link] */
SYNTAX_NODE(named_namespace_definition, "named-namespace-definition", "namespace.def")
SYNTAX_NODE(unnamed_namespace_definition, "unnamed-namespace-definition", "namespace.def")
SYNTAX_NODE(nested_namespace_definition, "nested-namespace-definition", "namespace.def")
SYNTAX_NODE(enclosing_namespace_specifier, "enclosing-namespace-specifier", "namespace.def")
SYNTAX_NODE(namespace_alias_definition, "namespace-alias-definition", "namespace.alias")
SYNTAX_NODE(qualified_namespace_specifier, "qualified-namespace-specifier", "namespace.alias")
SYNTAX_NODE(using_directive, "using-directive", "namespace.udir")
SYNTAX_NODE(linkage_specification, "linkage-specification", "dcl.link")

/*
 * Scaffolding, not grammar: both are counted, and both are at zero on
 * well-formed input when the parser is complete.
 *   unparsed   a balanced token sequence of a part of the grammar that is
 *              not written yet
 *   erroneous  what was skipped after a syntax error
 * Their tokens are the node's token range; they have no children.
 */
SYNTAX_NODE(unparsed, "(unparsed)", "")
SYNTAX_NODE(erroneous, "(erroneous)", "")
