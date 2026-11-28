/* [temp.expl.spec]/1.3: an explicit specialization of a class template's
   member function declared with a trailing return type */
struct tag {
};
template <class T> struct facet {
    virtual bool put(int x) const;
    auto get(int x) const -> int;
};
template <> auto facet<tag>::put(int x) const -> bool { return x == 1; }
template <> auto facet<tag>::get(int x) const -> int { return x + 1; }
int main()
{
    facet<tag> f;
    return !f.put(1) || f.put(2) || f.get(4) != 5;
}
