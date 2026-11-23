/* [class.access.base]/1, [class.dtor]/13: a protected virtual destructor of
   a base is accessible from the derived class's destructor and implicit
   constructors, and a dynamic class with such a base is analysed */
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;

void *operator new(size_t n) { return malloc(n); }
void operator delete(void *p) { free(p); }
void operator delete(void *p, size_t) { free(p); }

class counted {
    long owners;

public:
    counted() : owners(0) {}
    explicit counted(long n) : owners(n) {}
    virtual void gone() = 0;
    long count() const { return owners; }

protected:
    virtual ~counted();
};

counted::~counted() { puts("~counted"); }

class weak_counted : private counted {
    long weak;

public:
    explicit weak_counted(long n = 0) : counted(n), weak(n) {}
    long both() const { return weak + count(); }

protected:
    ~weak_counted() override;
};

weak_counted::~weak_counted() { puts("~weak_counted"); }

class impl : public weak_counted {
public:
    impl() : weak_counted(2) {}
    void gone() override { puts("gone"); }
    ~impl() { puts("~impl"); }
};

class implicit_base : protected counted {
public:
    void gone() override {}
};

int main()
{
    impl *p = new impl;
    printf("%ld\n", p->both());
    p->gone();
    delete p;
    implicit_base b;
    b.gone();
    return 0;
}
