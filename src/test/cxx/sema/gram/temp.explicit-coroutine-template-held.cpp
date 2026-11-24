// [temp.explicit]/10, [dcl.fct.def.coroutine]: an explicit instantiation of
// a coroutine template whose own declaration the analysis does not follow
// is held, not accepted (the promise is looked up in the instantiated body).
template <class T> T run(T x) { co_await x; return x; }
template int run(int);
