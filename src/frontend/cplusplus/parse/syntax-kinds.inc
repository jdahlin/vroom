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
SYNTAX_NODE(attribute_declaration, "attribute-declaration", "dcl.pre")
/* [dcl.spec], [dcl.type] */
SYNTAX_NODE(decl_specifier_seq, "decl-specifier-seq", "dcl.spec")
SYNTAX_NODE(type_specifier_seq, "type-specifier-seq", "dcl.type")
SYNTAX_NODE(defining_type_specifier_seq, "defining-type-specifier-seq", "dcl.type")
SYNTAX_NODE(simple_type_specifier, "simple-type-specifier", "dcl.type.simple")
SYNTAX_NODE(elaborated_type_specifier, "elaborated-type-specifier", "dcl.type.elab")
SYNTAX_NODE(elaborated_enum_specifier, "elaborated-enum-specifier", "dcl.type.elab")
SYNTAX_NODE(decltype_specifier, "decltype-specifier", "dcl.type.decltype")
SYNTAX_NODE(placeholder_type_specifier, "placeholder-type-specifier", "dcl.spec.auto")
SYNTAX_NODE(class_specifier, "class-specifier", "class.pre")
/* [dcl.enum], [enum.udecl] */
SYNTAX_NODE(enum_specifier, "enum-specifier", "dcl.enum")
SYNTAX_NODE(enum_head, "enum-head", "dcl.enum")
SYNTAX_NODE(enum_head_name, "enum-head-name", "dcl.enum")
SYNTAX_NODE(opaque_enum_declaration, "opaque-enum-declaration", "dcl.enum")
SYNTAX_NODE(enum_key, "enum-key", "dcl.enum")
SYNTAX_NODE(enum_base, "enum-base", "dcl.enum")
SYNTAX_NODE(enumerator_list, "enumerator-list", "dcl.enum")
SYNTAX_NODE(enumerator_definition, "enumerator-definition", "dcl.enum")
SYNTAX_NODE(enumerator, "enumerator", "dcl.enum")
SYNTAX_NODE(using_enum_declaration, "using-enum-declaration", "enum.udecl")
/* [expr.prim.id] */
SYNTAX_NODE(nested_name_specifier, "nested-name-specifier", "expr.prim.id.qual")
SYNTAX_NODE(qualified_id, "qualified-id", "expr.prim.id.qual")
SYNTAX_NODE(unqualified_id, "unqualified-id", "expr.prim.id.unqual")
/* [expr.prim] to [expr.comma]: a literal, this and an identifier are their
   token; the productions with one operand pass it on */
SYNTAX_NODE(primary_expression, "primary-expression", "expr.prim")
SYNTAX_NODE(postfix_expression, "postfix-expression", "expr.post")
SYNTAX_NODE(unary_expression, "unary-expression", "expr.unary")
SYNTAX_NODE(noexcept_expression, "noexcept-expression", "expr.unary.noexcept")
SYNTAX_NODE(new_expression, "new-expression", "expr.new")
SYNTAX_NODE(new_placement, "new-placement", "expr.new")
SYNTAX_NODE(new_type_id, "new-type-id", "expr.new")
SYNTAX_NODE(new_declarator, "new-declarator", "expr.new")
SYNTAX_NODE(noptr_new_declarator, "noptr-new-declarator", "expr.new")
SYNTAX_NODE(new_initializer, "new-initializer", "expr.new")
SYNTAX_NODE(delete_expression, "delete-expression", "expr.delete")
SYNTAX_NODE(cast_expression, "cast-expression", "expr.cast")
SYNTAX_NODE(pm_expression, "pm-expression", "expr.mptr.oper")
SYNTAX_NODE(multiplicative_expression, "multiplicative-expression", "expr.mul")
SYNTAX_NODE(additive_expression, "additive-expression", "expr.add")
SYNTAX_NODE(shift_expression, "shift-expression", "expr.shift")
SYNTAX_NODE(compare_expression, "compare-expression", "expr.spaceship")
SYNTAX_NODE(relational_expression, "relational-expression", "expr.rel")
SYNTAX_NODE(equality_expression, "equality-expression", "expr.eq")
SYNTAX_NODE(and_expression, "and-expression", "expr.bit.and")
SYNTAX_NODE(exclusive_or_expression, "exclusive-or-expression", "expr.xor")
SYNTAX_NODE(inclusive_or_expression, "inclusive-or-expression", "expr.or")
SYNTAX_NODE(logical_and_expression, "logical-and-expression", "expr.log.and")
SYNTAX_NODE(logical_or_expression, "logical-or-expression", "expr.log.or")
SYNTAX_NODE(conditional_expression, "conditional-expression", "expr.cond")
SYNTAX_NODE(assignment_expression, "assignment-expression", "expr.ass")
SYNTAX_NODE(expression, "expression", "expr.comma")
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
SYNTAX_NODE(braced_init_list, "braced-init-list", "dcl.init")
SYNTAX_NODE(initializer_list, "initializer-list", "dcl.init")
SYNTAX_NODE(designated_initializer_list, "designated-initializer-list", "dcl.init")
SYNTAX_NODE(designated_initializer_clause, "designated-initializer-clause", "dcl.init")
SYNTAX_NODE(designator, "designator", "dcl.init")
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
/* [namespace.udecl], [dcl.asm] */
SYNTAX_NODE(using_declaration, "using-declaration", "namespace.udecl")
SYNTAX_NODE(using_declarator_list, "using-declarator-list", "namespace.udecl")
SYNTAX_NODE(using_declarator, "using-declarator", "namespace.udecl")
SYNTAX_NODE(asm_declaration, "asm-declaration", "dcl.asm")
/* [dcl.attr.grammar] */
SYNTAX_NODE(attribute_specifier_seq, "attribute-specifier-seq", "dcl.attr.grammar")
SYNTAX_NODE(attribute_specifier, "attribute-specifier", "dcl.attr.grammar")
SYNTAX_NODE(alignment_specifier, "alignment-specifier", "dcl.attr.grammar")
SYNTAX_NODE(attribute_using_prefix, "attribute-using-prefix", "dcl.attr.grammar")
SYNTAX_NODE(attribute_list, "attribute-list", "dcl.attr.grammar")
SYNTAX_NODE(attribute, "attribute", "dcl.attr.grammar")
SYNTAX_NODE(attribute_scoped_token, "attribute-scoped-token", "dcl.attr.grammar")
SYNTAX_NODE(attribute_argument_clause, "attribute-argument-clause", "dcl.attr.grammar")
SYNTAX_NODE(balanced_token_seq, "balanced-token-seq", "dcl.attr.grammar")
SYNTAX_NODE(balanced_token, "balanced-token", "dcl.attr.grammar")

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
