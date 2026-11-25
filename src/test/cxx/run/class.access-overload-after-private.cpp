/* [class.access]/4: access is checked for the overload selected, whatever
   the access of the others of the name */
class H
{
    int n = 3;
    int &size()
    {
        return n;
    }

public:
    int size() const
    {
        return n + 1;
    }
};
template <class T> struct U {
    H t;
    int size() const
    {
        return t.size();
    }
};
int main()
{
    U<int> const u{};
    H const h{};
    return !(u.size() == 4 && h.size() == 4);
}
