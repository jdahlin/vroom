/*
 * declared-templates.h: what the files of template declarations
 * (declared-templates.cpp and the files split from it by kind) share.
 */
#ifndef VCC_CPLUSPLUS_DECLARED_TEMPLATES_H
#define VCC_CPLUSPLUS_DECLARED_TEMPLATES_H

namespace cplusplus
{

/* whether a type is made of a template-parameter type, of one at position
   from or after if from is given (declared-templates.cpp) */
VPRIV bool made_of_template_parameter(const types &table, qualified_type t,
                                      const entities *parameters = nullptr,
                                      uint64_t from = 0);
/* [temp.mem.class]/1: the simple-template-id O<T> of a member class of a
   class template defined outside it, and optionally the member class
   name's token (declared-templates.cpp) */
VPRIV SyntaxRef out_of_class_class_member(SyntaxRef only,
                                          token_index *name = nullptr,
                                          bool partial = false);

} // namespace cplusplus

#endif
