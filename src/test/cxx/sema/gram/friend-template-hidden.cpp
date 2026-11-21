// [basic.lookup.argdep]/4.3: a friend template first declared in a class
// is visible by ADL only, until a matching namespace declaration.
namespace before {
struct X { template<class T> friend int hidden(T) { return 1; } };
int no_adl() { return hidden(1); }
}
namespace after {
struct X { template<class T> friend int hidden(T) { return 2; } };
template<class T> int hidden(T);
int ordinary() { return hidden(1); }
}
namespace qualified {
struct X { template<class T> friend int hidden(T) { return 3; } };
int no_qualified_lookup() { return qualified::hidden(1); }
}
namespace overload {
int choose(int);
struct X { template<class T> friend int choose(T) { return 4; } };
int ordinary() { return choose(1); }
}
namespace associated {
struct X { template<class T> friend int hidden(T) { return 5; } };
struct Y {};
int wrong_association() { return hidden(Y{}); }
int right_association() { return hidden(X{}); }
}
namespace hidden_operator {
struct X { template<class T> friend int operator+(T, T) { return 1; } };
struct Y {};
int bad() { return Y{} + Y{}; }
int good() { return X{} + X{}; }
}
