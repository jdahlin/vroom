/* cxx-run-units: vague-linkage-2.cpp */
/* [dcl.inline]/6, [basic.def.odr]/13: a static local of an inline or
   templated function is one object in the program, and a specialization
   over a local class or closure type of such a function one function
   ([impl.clang] VisibleNoLinkage): the other unit counts on in this one's
   objects */
#include "units/vague-linkage.h"

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

int other();

int main()
{
    check(count(closure()) == 101);
    check(count(local_class()) == 102);
    check(bump() == 1);
    check(bump_in_lambda() == 1);
    check(templated(3) == 103);
    check(templated_local(4) == 4);
    check(other() == 201 + 202 + 2 + 2 + 203 + 4);
    return 0;
}
