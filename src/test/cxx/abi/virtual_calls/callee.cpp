#include "shared.h"

Shape::Shape(int id) : id(id)
{
}

int Shape::area() const
{
    return 0;
}

int Shape::scaled(int by) const
{
    return area() * by + id;
}

Square::Square(int id, int side) : Shape(id), side(side)
{
}

int Square::area() const
{
    return side * side;
}

const char *Named::name() const
{
    return "unnamed";
}

int total_area(const Shape *const *shapes, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += shapes[i]->area() + shapes[i]->scaled(2);
    return total;
}

const char *name_of(const Named &n)
{
    return n.name();
}

static Square the_square(3, 5);

const Shape *callee_square()
{
    return &the_square;
}
