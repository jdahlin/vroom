/* cxx-run-units: internal-linkage-2.cpp internal-linkage-3.cpp */
/* [basic.link]/4: specializations with arguments of one translation unit,
   closure types of namespace scope and what is declared in them are each
   unit's own: three units with the same names for different entities. */
#define UNIT 3
#define NAME run_main
#include "units/internal-linkage-body.h"
int run_a();
int run_b();
int main()
{
    int a = run_a();
    int b = run_b();
    int c = run_main();
    printf("%d %d %d\n", a, b, c);
    return 0;
}
