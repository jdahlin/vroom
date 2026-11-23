/* [temp.class.spec]/1: a partial specialization declared before its
   definition; [temp.class]/1 with [dcl.type.elab]: a class template
   declared with union; [dcl.attr.grammar]: [[gnu::aligned]] in a class
   template's head. */
template <class T, class A = T>
class vector;

template <class A>
class vector<bool, A>;

template <class A>
class vector<bool, A> {
public:
    int tag() const { return 2; }
};

template <class T, class A>
class vector {
public:
    int tag() const { return 1; }
};

enum kind { first, second };

template <kind K, unsigned long N, class... Ts>
union packed;

template <kind K, unsigned long N, class... Ts>
union packed {
    char bytes[N];
    int word;
};

template <class T>
struct [[__gnu__::__aligned__(alignof(T))]] cookie {
    unsigned long count;
};

int main()
{
    vector<int> a;
    vector<bool, int> b;
    packed<first, 8> p;
    p.word = 3;
    if (a.tag() != 1 || b.tag() != 2 || sizeof(p) != 8 || p.word != 3)
        return 1;
    return 0;
}
