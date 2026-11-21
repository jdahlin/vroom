// [class.access], [class.default.ctor]/2: an inaccessible defaulted
// constructor deletes an implicit constructor that needs it. Friends and
// derived classes can use private and protected constructors respectively.
struct Private {
    int data;
private:
    Private() = default;
    friend struct Friend;
};
struct Friend : Private { Friend() {} };
struct NotFriend : Private {};
struct Protected {
    int data;
protected:
    Protected() = default;
};
struct Derived : Protected { Derived() {} };
struct Member { Protected value; };
void valid() { Friend f; Derived d; }
void private_use() { Private p; }
void deleted_derived() { NotFriend n; }
void deleted_member() { Member m; }
struct DerivedWithMember : Protected { Protected member; };
static_assert(!__is_constructible(DerivedWithMember));
struct Reference {
private:
    Reference() = default;
public:
    int &value;
};
static_assert(!__is_constructible(Reference));
struct TrivialBase { int value; protected: TrivialBase() = default; };
struct TrivialDerived : TrivialBase {};
static_assert(__is_trivially_constructible(TrivialDerived));
static_assert(__is_trivially_copyable(TrivialBase));
