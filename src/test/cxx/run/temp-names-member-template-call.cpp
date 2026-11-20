/* [temp.names]: explicit template arguments on a member function template and a call through -> */
struct S { template <class T> T get() const { return T(4); } };
int main() { S s; S *p = &s; return s.get<int>() - 4 + p->get<char>() - 4 + p->template get<short>() - 4; }
