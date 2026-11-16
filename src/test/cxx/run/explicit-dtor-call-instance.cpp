// p->~T() of a class template's instance whose destructor nothing else uses: its body instantiated (json with an
// allocator whose construct throws: std::__destroy_at<vector<json>> alone calls ~vector)
int gone;
template <class T> struct V { T *p = nullptr; ~V(); };
template <class T> V<T>::~V() { gone++; }
template <class T> void destroy_at(T *p) { p->~T(); }
int main() { alignas(V<int>) unsigned char buf[sizeof(V<int>)]; destroy_at(reinterpret_cast<V<int> *>(buf)); return gone == 1 ? 0 : 1; }
