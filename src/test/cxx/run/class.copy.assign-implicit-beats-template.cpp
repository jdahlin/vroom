/* [over.match.best]/2.4: the implicit copy assignment operator is better
   than an assignment operator template's specialization taking the class
   by value */
template <class It> struct Iter {
    It map_iter{};
    template <class T> Iter &operator=(T mi)
    {
        this->map_iter = mi;
        return *this;
    }
};
int main()
{
    Iter<int> a, b;
    a = 3;
    b = a;
    return b.map_iter - 3;
}
