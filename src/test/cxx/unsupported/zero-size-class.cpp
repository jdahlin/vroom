/* A class whose data members are all flexible array members or arrays of
   zero elements has size 0 with clang and GCC ([ext:gnu_flexible_array]);
   analysis takes a size of 0 for an incomplete class's, so the definition
   is refused rather than given a size of 1 that sizeof would report. */
struct Flexible {
    int values[];
};
static_assert(sizeof(Flexible) == 0);
