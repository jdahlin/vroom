// member templates defined outside their class template (template<class T> template<class... A>), with
// and without a parameter pack: their bodies' code, the class's members by name in them
template <class T> struct V {
	T last = 0;
	template <class... A> T emplace(A &&...a);
	template <class U> T one(U u);
	template <class U> T inl(U u) { return last = (T)u; }
};
template <class T> template <class... A> T V<T>::emplace(A &&...a) { return last = T(static_cast<A &&>(a)...); }
template <class T> template <class U> T V<T>::one(U u) { return last = (T)u + 1; }
int main() { V<int> v; return v.emplace(5) == 5 && v.one(2L) == 3 && v.inl('a') == 'a' ? 0 : 1; }
