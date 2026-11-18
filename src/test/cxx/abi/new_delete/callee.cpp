#include "shared.h"

Animal::Animal(int legs) : legs(legs)
{
}

Animal::~Animal()
{
    destroyed += legs;
}

int Animal::sound() const
{
    return 1;
}

Animal *callee_make(int which)
{
    if (which)
        return new Dog;
    return new Animal(2);
}

void callee_delete(Animal *a)
{
    delete a;
}

Counted *callee_array(size_t n)
{
    return new Counted[n];
}

void callee_delete_array(Counted *c)
{
    delete[] c;
}
