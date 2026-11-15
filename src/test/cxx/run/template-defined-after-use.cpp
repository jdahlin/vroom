// a function template used before its definition: instantiated from the definition at the TU's end (doctest's
// TEST_CASE_TEMPLATE); a static one's instances are internal (_ZL..., local)
template <class T> static int func();
int use() { return func<int>() + func<char>(); }
template <class T> static int func() { return sizeof(T); }
int main() { return use() == 5 ? 0 : 1; }
