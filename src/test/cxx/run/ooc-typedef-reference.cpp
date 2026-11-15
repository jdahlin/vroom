// out-of-class member definitions told apart by reference kind when one parameter is a typedef of a reference
// (libc++'s vector::insert(const_iterator, const_reference) and (const_iterator, value_type&&))
template <class T> struct V {
	typedef T value_type;
	typedef const T &const_reference;
	typedef const T *const_iterator;
	int k = 0;
	int insert(const_iterator p, const_reference x);
	int insert(const_iterator p, value_type &&x);
};
template <class T> int V<T>::insert(const_iterator p, const_reference x) { return 1; }
template <class T> int V<T>::insert(const_iterator p, value_type &&x) { return 2; }
int main() { V<unsigned char> v; unsigned char c = 3; return v.insert(nullptr, c) * 10 + v.insert(nullptr, 4) - 12; }
