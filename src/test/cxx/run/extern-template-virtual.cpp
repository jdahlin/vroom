// [temp.explicit]/11, [temp.inst]/11: an explicit instantiation declaration
// of a class template does not instantiate its virtual member functions, whose
// bodies here use a class that is never defined.
template <class C> struct buffer;
template <class C> struct iterator_ {
    buffer<C> *b_;
};
template <class C> int pad(iterator_<C> s)
{
    return s.b_->get();
}
struct base {
    virtual ~base() {}
};
template <class C, class It = iterator_<C> >
struct facet : base {
    typedef It iter_type;
    int put(iter_type s) const { return do_put(s); }
protected:
    virtual int do_put(iter_type s) const;
};
template <class C, class It>
int facet<C, It>::do_put(iter_type s) const
{
    return pad(s);
}
extern template struct facet<char>;

int main()
{
    return 0;
}
