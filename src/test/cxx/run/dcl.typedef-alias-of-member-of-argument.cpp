/* [dcl.typedef]/2: an alias-declaration's type-id is a type: Node::Key where
   Node is an alias of a template parameter's alias, through a
   specialization */
template <class T> struct node_list {
    using Item = T;
};
struct node {
    using Key = long;
};
using list = node_list<node>;
struct types {
    using NodeList = list;
};
template <class Types> struct base {
    typedef typename Types::NodeList::Item Node;
};
struct Y : base<types> {
    using Node = types::NodeList::Item;
    using Key = Node::Key;
    Key k = 5;
};
int main()
{
    Y y;
    return sizeof(y.k) != sizeof(long) || y.k != 5;
}
