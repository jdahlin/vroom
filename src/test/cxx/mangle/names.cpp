/* [abi:mangle.name], [abi:mangle.abi-tag], [abi:mangling-operator],
   [abi:mangling-special-ctor-dtor] */
namespace a
{
namespace b
{
struct C {
    void member();
    void const_member() const;
    void volatile_member() volatile;
    void lvalue_member() &;
    void rvalue_member() &&;
    static void static_member();
    struct Inner {
        void f();
    };
};
void C::member() {}
void C::const_member() const {}
void C::volatile_member() volatile {}
void C::lvalue_member() & {}
void C::rvalue_member() && {}
void C::static_member() {}
void C::Inner::f() {}
} // namespace b
inline namespace v1
{
void in_inline() {}
} // namespace v1
} // namespace a
namespace
{
void in_unnamed() {}
} // namespace
void call_unnamed() { in_unnamed(); }
namespace std
{
void in_std() {}
} // namespace std
void __attribute__((abi_tag("tag"))) tagged() {}
struct Ops {
    Ops operator+(const Ops &) const;
    Ops operator-() const;
    Ops &operator=(const Ops &);
    bool operator==(const Ops &) const;
    bool operator!() const;
    Ops &operator++();
    Ops operator++(int);
    int operator()(int x) const;
    int operator[](int x) const;
    Ops *operator->();
    Ops &operator<<=(int);
    operator int() const;
    void *operator new(unsigned long n);
    void operator delete(void *);
};
Ops Ops::operator+(const Ops &) const { return *this; }
Ops Ops::operator-() const { return *this; }
Ops &Ops::operator=(const Ops &) { return *this; }
bool Ops::operator==(const Ops &) const { return true; }
bool Ops::operator!() const { return false; }
Ops &Ops::operator++() { return *this; }
Ops Ops::operator++(int) { return *this; }
int Ops::operator()(int x) const { return x; }
int Ops::operator[](int x) const { return x; }
Ops *Ops::operator->() { return this; }
Ops &Ops::operator<<=(int) { return *this; }
Ops::operator int() const { return 0; }
void *Ops::operator new(unsigned long) { return nullptr; }
void Ops::operator delete(void *) {}
unsigned long long operator""_km(unsigned long long x) { return x; }
struct Base {
    Base();
    virtual ~Base();
};
Base::Base() {}
Base::~Base() {}
// MANGLE: _ZN1a1b1C6memberEv
// MANGLE: _ZNK1a1b1C12const_memberEv
// MANGLE: _ZNV1a1b1C15volatile_memberEv
// MANGLE: _ZNR1a1b1C13lvalue_memberEv
// MANGLE: _ZNO1a1b1C13rvalue_memberEv
// MANGLE: _ZN1a1b1C13static_memberEv
// MANGLE: _ZN1a1b1C5Inner1fEv
// MANGLE: _ZN1a2v19in_inlineEv
// MANGLE: _ZN12_GLOBAL__N_110in_unnamedEv
// MANGLE: _ZSt6in_stdv
// MANGLE: _Z6taggedB3tagv
// MANGLE: _ZNK3OpsplERKS_
// MANGLE: _ZNK3OpsngEv
// MANGLE: _ZN3OpsaSERKS_
// MANGLE: _ZNK3OpseqERKS_
// MANGLE: _ZNK3OpsntEv
// MANGLE: _ZN3OpsppEv
// MANGLE: _ZN3OpsppEi
// MANGLE: _ZNK3OpsclEi
// MANGLE: _ZNK3OpsixEi
// MANGLE: _ZN3OpsptEv
// MANGLE: _ZN3OpslSEi
// MANGLE: _ZNK3OpscviEv
// MANGLE: _ZN3OpsnwEm
// MANGLE: _ZN3OpsdlEPv
// MANGLE: _Zli3_kmy
// MANGLE: _ZN4BaseC1Ev
// MANGLE: _ZN4BaseC2Ev
// MANGLE: _ZN4BaseD0Ev
// MANGLE: _ZN4BaseD1Ev
// MANGLE: _ZN4BaseD2Ev
