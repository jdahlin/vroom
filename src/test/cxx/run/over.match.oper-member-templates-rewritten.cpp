/* [over.match.best]/2: a member operator!= template, or operator== template,
   against the reversed operator== template of the same class: partial
   ordering separates neither, and the rewritten candidate loses
   (nlohmann::json's iter_impl). */
template <class T> struct It {
    int p;
    template <class U> bool operator==(const U &o) const { return p == o.p; }
    template <class U> bool operator!=(const U &o) const { return p != o.p; }
};

int main()
{
    It<int> a{1}, b{2};
    if (!(a != b))
        return 1;
    if (a == b)
        return 2;
    if (a != a)
        return 3;
    return 0;
}
