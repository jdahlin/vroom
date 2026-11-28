/* [basic.start.dynamic], [dcl.init.general]/7.2: arrays of a class with a
   non-constant default constructor at namespace and block scope, initialized
   element by element and destroyed in reverse order
   ([basic.start.term]). */
extern "C" int printf(const char *, ...);

static int next_id;

struct Item {
    int id;
    int members[3] = {1, 2, 3};
    Item() : id(++next_id) {}
    ~Item() { printf("~%d\n", id); }
};

struct Plain {
    int id;
    Plain() : id(++next_id) {}
};

struct Holder {
    Item item;
    int n = 7;
};

Item items[3];
static Item grid[2][2];
Plain plains[4];
Holder holders[2];

namespace space {
Plain more[2];
}

Item &local()
{
    static Item once[2];
    return once[1];
}

int main()
{
    for (int i = 0; i < 3; i++)
        printf("%d %d\n", items[i].id, items[i].members[2]);
    printf("%d %d\n", grid[0][0].id, grid[1][1].id);
    printf("%d %d\n", plains[0].id, plains[3].id);
    printf("%d %d %d\n", holders[0].item.id, holders[1].item.id,
           holders[1].n);
    printf("%d %d\n", space::more[1].id, local().id);
    printf("%d\n", next_id);
    return 0;
}
