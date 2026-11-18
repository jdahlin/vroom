/* [class.virtual] across compilers: each half calls virtual functions
   through the virtual tables the other half emitted ([abi:vtable]), one
   of them through a secondary base's (a thunk). */
#ifndef SHARED_H
#define SHARED_H

/* its virtual table is where area is defined: the callee */
struct Shape {
    Shape(int id);
    virtual int area() const;
    virtual int scaled(int by) const;
    int id;
};

struct Square : Shape {
    Square(int id, int side);
    int area() const override;
    int side;
};

struct Named {
    virtual const char *name() const;
    int tag;
};

/* its virtual tables are the caller's */
struct Both : Square, Named {
    Both(int side);
    int area() const override;
    const char *name() const override;
};

int total_area(const Shape *const *shapes, int n);
const char *name_of(const Named &n);
const Shape *callee_square();

#endif
