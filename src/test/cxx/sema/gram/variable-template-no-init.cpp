template<typename T> int vartemplate_b;
template<class T> constexpr T cv = T();
int f() { return vartemplate_b<int>; }
