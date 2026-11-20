/* [temp.mem]/1, [temp.friend]: a member function template of one class befriended by another (all its specializations), and a friend function template defined in a class, reaching private members */
struct F;
struct G {
    template <class T> int peek(const F &, T);
};
struct F {
    template <class T> friend int G::peek(const F &, T);
    template <class T> friend int look(const F &f, T t) { return f.secret + int(t); }
private:
    int secret = 5;
};
template <class T> int G::peek(const F &f, T t)
{
    return f.secret * 2 + int(t);
}
int main()
{
    G g;
    F f;
    return g.peek(f, 1) - 11 + g.peek(f, 'a') - 107 + look(f, 1) - 6;
}
