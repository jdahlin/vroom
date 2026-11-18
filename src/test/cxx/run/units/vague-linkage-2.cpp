#include "vague-linkage.h"

int other()
{
    return count(closure()) + count(local_class()) + bump() +
           bump_in_lambda() + templated(3) + templated_local(4);
}
