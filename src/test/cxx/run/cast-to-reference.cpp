// (T&)x of an object of another type: that object read as a T, *(T *)&x (libc++'s
// operator>>(istream&, unsigned char&) is return is >> (char&)__c)
struct S { int v; };
int f1(unsigned char &u) { char &c = (char &)u; c = 5; return u; }
int f2(int &i) { return (long &)i, 0; }
int f3(S &s) { const S &r = (const S &)s; return r.v; }
int f4(unsigned char &u) { (char &)u = 7; return u; }
int main() { unsigned char u = 0; S s{3}; int i = 0; return f1(u) == 5 && f3(s) == 3 && f4(u) == 7 ? 0 : 1; }
