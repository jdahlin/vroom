// [temp.explicit]/1: an explicit instantiation of a variable template
template <class T> int mt = sizeof(T);
template int mt<int *>;
template int mt<char>;
template <class T> const int ct = sizeof(T) + 1;
template const int ct<short>;
int main() { return mt<char> + ct<short> == 3 ? 0 : 1; }
