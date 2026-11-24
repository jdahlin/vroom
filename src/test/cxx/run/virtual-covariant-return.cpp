/* [class.virtual]/8: covariant return types (pointer and reference, a base at offset 0): the override is called through the base's declaration */
struct S { virtual ~S(){} virtual S* dup() const = 0; virtual S& self() = 0; virtual int id() const { return 1; } };
struct R : S { virtual R* dup() const override = 0; };
struct T : R { int x=5; T* dup() const override { return new T(*this); } T& self() override { return *this; } int id() const override { return 2; } };
int main(){ T t; S* s=&t; S* d=s->dup(); if(d->id()!=2) return 1; if(&s->self()!=s) return 2; R* r=&t; R* d2=r->dup(); if(d2->id()!=2) return 3; return 0; }
