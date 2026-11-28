/* [temp.expl.spec]/1.3: explicit specialization of a class template
   specialization's member operator function */
typedef unsigned int uint;
struct L {
};
template <class T> struct removal {
    uint max;
    uint preprocess(uint p);
    bool operator()(uint p);
    int operator[](int i) const;
};
template <class T> uint removal<T>::preprocess(uint p) { return max + p; }
template <> bool removal<L>::operator()(uint p)
{
    uint v = this->preprocess(p);
    return v == 3;
}
template <> int removal<L>::operator[](int i) const { return i * 2; }
template uint removal<L>::preprocess(uint p);
int main()
{
    removal<L> r{1};
    return !r(2) || r[4] != 8;
}
