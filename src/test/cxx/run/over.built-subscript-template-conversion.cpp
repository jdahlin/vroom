/* [over.built]: a conversion function template gives no built-in subscript
   candidate to compete with a member operator[] whose index is converted */
struct J {
    int a[3] = {4, 5, 6};
    template <class V> operator V() const
    {
        return V();
    }
    int &operator[](unsigned long i)
    {
        return a[i];
    }
};
int main()
{
    J j;
    j[1] = 9;
    return !(j[0] == 4 && j[1] == 9 && j[2] == 6);
}
