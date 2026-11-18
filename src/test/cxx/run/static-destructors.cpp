// [basic.start.term]/1, /3: objects of static storage duration are
// destroyed after main returns, in the reverse order of the completion of
// their constructors; [abi:dso-dtor]: each destructor registered with
// __cxa_atexit(destructor, object, &__dso_handle) once the object is
// constructed, a constant-initialized one at start-up as well
#include <stdio.h>

struct Noisy {
    int id;
    Noisy(int i) : id(i) { printf("construct %d\n", id); }
    ~Noisy() { printf("destroy %d\n", id); }
};

struct Plain {
    int id;
    ~Plain();
};
Plain::~Plain() { printf("destroy plain %d\n", id); }

struct Holder {
    Noisy member;
    Holder(int i) : member(i) {}
};

Noisy first(1);
Plain constant = {2};
Noisy second(3);
Holder holder(4);
static Noisy internal(5);

int main()
{
    printf("main %d %d %d %d %d\n", first.id, constant.id, second.id,
           holder.member.id, internal.id);
    return 0;
}
