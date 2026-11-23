/* [temp.param]/14: a function template with two template parameter packs,
   both deduced from the parameters' types */
template <class... T> struct list {};

template <class... T, class... U>
int count(list<T...>, list<U...>)
{
    return sizeof...(T) * 10 + sizeof...(U);
}

template <class... T, class... U>
int size(list<T...>, list<U...>)
{
    return (0 + ... + sizeof(T)) * 100 + (0 + ... + sizeof(U));
}

int main()
{
    if (count(list<int, char>(), list<char, char, short>()) != 23)
        return 1;
    if (count(list<>(), list<int>()) != 1)
        return 2;
    if (size(list<int, char>(), list<short>()) != 502)
        return 3;
    return 0;
}
