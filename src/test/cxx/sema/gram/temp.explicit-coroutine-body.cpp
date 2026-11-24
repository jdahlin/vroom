// [temp.explicit]/10: an explicit instantiation definition of a function
// template instantiates its body, where a coroutine's promise is looked up
// ([dcl.fct.def.coroutine]); the body is not analysed, so the instantiation
// is held rather than accepted.
template <class T> void f(T) { co_return 1; }
template void f(int);
