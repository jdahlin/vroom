// [class.mem]/6: inline member bodies see the completed class, including
// deallocation functions declared after the body and in base classes.
#include <stdlib.h>
typedef unsigned long size_t;
static int freed;
void *operator new(size_t n) { return malloc(n); }
void operator delete(void *p) { free(p); }
void operator delete(void *p, size_t) { free(p); }
void *operator new[](size_t n) { return malloc(n); }
void operator delete[](void *p) { free(p); }

struct Base {
    virtual ~Base() {}
    virtual void dispose() { delete this; }
    void operator delete(void *p) { freed += 1; free(p); }
};
struct Derived : Base { ~Derived() {} };
struct Later {
    virtual ~Later() {}
    void dispose() { delete this; }
    void operator delete(void *p) { freed += 10; free(p); }
};

struct Array {
    int value;
    void operator delete[](void *p, size_t) { freed += 100; free(p); }
};
void dispose_array(Array *p) { delete[] p; }

// The spelling in another class does not affect lookup in Base or Later.
struct Unrelated { void operator delete(void *p) { free(p); } };
int main() {
    Base *a = new Derived;
    a->dispose();
    Later *b = new Later;
    b->dispose();
    dispose_array(new Array[2]);
    return freed != 111;
}
