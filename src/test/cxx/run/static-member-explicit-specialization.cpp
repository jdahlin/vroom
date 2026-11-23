/* [temp.expl.spec]/1.5 and [temp.explicit]/10: the static data member of a
   class template's specialization, defined by an explicit specialization
   (its value the specialization's, not the template's) and explicitly
   instantiated (emitted whether used or not). */
template <class T> struct X {
    static int value;
    static int other;
};
template <class T> int X<T>::value = 5;
template <class T> int X<T>::other = 7;

template <> int X<float>::value = 1;
template int X<char>::other;

int main()
{
    if (X<float>::value != 1 || X<char>::value != 5)
        return 1;
    if (X<float>::other != 7)
        return 2;
    return 0;
}
