/* [abi:mangling-special]: virtual tables, RTTI, guards, temporaries,
   thunks */
struct P {
    virtual void f() {}
    virtual ~P() {}
};
struct Q {
    virtual void g() {}
};
struct R : P, Q {
    void g() override {}
};
struct Data {
    int x;
};
struct VB : virtual Data {
    virtual void h() {}
};
R r;
VB vb;
int value();
inline int &guarded()
{
    static int x = value();
    return x;
}
int use_guarded() { return guarded(); }
const int &extended = value();
inline int abc = value();
int use_abc() { return abc; }
const P &type_of(const P &p) { return p; }
// MANGLE: _ZTI1P
// MANGLE: _ZTS1P
// MANGLE: _ZTV1R
// MANGLE: _ZThn8_N1R1gEv
// MANGLE: _ZTT2VB
// MANGLE: _ZGVZ7guardedvE1x
// MANGLE: _ZGR8extended_
// MANGLE: _ZGV3abc
