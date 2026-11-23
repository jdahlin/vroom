/* [abi:class-types]: a base class that is not a POD for the purpose of
   layout takes its data size, so a derived class's members go into its
   tail padding. */
#ifndef SHARED_H
#define SHARED_H

struct Base {
    Base(int i, char c);
    int i;
    char c;
};

/* d at offset 5 */
struct Derived : Base {
    Derived(int i, char c, char d);
    char d;
};

/* e at offset 6, the size still 8 */
struct Twice : Derived {
    Twice(int i, char c, char d, char e);
    char e;
};

int sum(const Twice &t);
Twice make(int i);
Derived step(Derived d);
void fill(Base *b, int i, char c);

#endif
