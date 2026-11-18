/* vcc-xfail: doc/PLAN-cxx-sema.md S4: [class.virtual] is not analysed */
#include <stdio.h>

#include "shared.h"

Both::Both(int side) : Square(9, side)
{
    tag = side;
}

int Both::area() const
{
    return side * side + 1;
}

const char *Both::name() const
{
    return "both";
}

int main()
{
    Shape plain(1);
    Square square(2, 4);
    Both both(6);
    Named named;
    named.tag = 0;
    const Shape *shapes[4] = {&plain, &square, &both, callee_square()};
    for (int i = 0; i < 4; i++)
        printf("shape %d area %d scaled %d\n", shapes[i]->id, shapes[i]->area(),
               shapes[i]->scaled(3));
    printf("total %d\n", total_area(shapes, 4));
    const Named &as_named = both;
    printf("names %s %s %s\n", name_of(named), name_of(both), as_named.name());
    printf("sizes %d %d %d %d\n", static_cast<int>(sizeof(Shape)),
           static_cast<int>(sizeof(Square)), static_cast<int>(sizeof(Named)),
           static_cast<int>(sizeof(Both)));
    return 0;
}
