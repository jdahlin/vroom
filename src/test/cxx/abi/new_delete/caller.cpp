#include <stdio.h>
#include <stdlib.h>

#include "shared.h"

int destroyed;
static int allocations;

void *operator new(size_t n)
{
    allocations++;
    return malloc(n);
}
void *operator new[](size_t n)
{
    allocations++;
    return malloc(n);
}
void operator delete(void *p)
{
    allocations--;
    free(p);
}
void operator delete(void *p, size_t)
{
    allocations--;
    free(p);
}
void operator delete[](void *p)
{
    allocations--;
    free(p);
}
void operator delete[](void *p, size_t)
{
    allocations--;
    free(p);
}

static int next_value;

Counted::Counted() : value(next_value++)
{
}

Counted::~Counted()
{
    destroyed += 100;
}

Dog::Dog() : Animal(4), bones(3)
{
}

Dog::~Dog()
{
    destroyed += 10 * bones;
}

int Dog::sound() const
{
    return 2;
}

int main()
{
    Animal *a = callee_make(0);
    Animal *d = callee_make(1);
    printf("sounds %d %d\n", a->sound(), d->sound());
    delete a; /* the callee's deleting destructor */
    delete d; /* the caller's */
    printf("destroyed %d allocations %d\n", destroyed, allocations);
    callee_delete(new Dog);
    callee_delete(new Animal(1));
    printf("destroyed %d allocations %d\n", destroyed, allocations);
    Counted *c = callee_array(3);
    printf("values %ld %ld %ld\n", c[0].value, c[1].value, c[2].value);
    delete[] c;
    Counted *e = new Counted[2];
    callee_delete_array(e);
    printf("destroyed %d allocations %d\n", destroyed, allocations);
    return 0;
}
