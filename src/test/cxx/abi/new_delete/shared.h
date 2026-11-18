/* [expr.new] and [expr.delete] across compilers: objects allocated in one
   half are deleted in the other, through a base's virtual destructor (the
   deleting destructor in the virtual table, [abi:vtable-components]) and
   as arrays whose count is in the array cookie ([abi:array-cookies]). */
#ifndef SHARED_H
#define SHARED_H

typedef unsigned long size_t;

extern int destroyed;

/* its virtual table and deleting destructor are the callee's */
struct Animal {
    Animal(int legs);
    virtual ~Animal();
    virtual int sound() const;
    int legs;
};

/* the caller's */
struct Dog : Animal {
    Dog();
    ~Dog();
    int sound() const;
    int bones;
};

/* a non-trivial destructor: arrays of it have a cookie */
struct Counted {
    Counted();
    ~Counted();
    long value;
};

Animal *callee_make(int which);
void callee_delete(Animal *a);
Counted *callee_array(size_t n);
void callee_delete_array(Counted *c);

#endif
