// a function template instance's signature, known from its first use, stays known when a later instantiation of
// its body cannot give it again: the next call with the same arguments still ranks it (json's operator[](T *)
// for two string literals under JSON_DIAGNOSTICS)
template <class T> struct J {
	int v = 0;
	template <class K> int at(K *k) { return k[0] + v; }
	template <class K> int at(K &&k) const { return 100; }
};
int main() { J<int> j; return j.at("a") == 'a' && j.at("bc") == 'b' ? 0 : 1; }
