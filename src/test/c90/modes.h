#define TEXT0(x) #x
#define TEXT(x) TEXT0(x)
#define JOIN_DIGRAPH(a,b) TEXT(a %:%: b)
static const char *joined = JOIN_DIGRAPH(1,2);
static const char *brackets = TEXT(<: :> <% %> %:);
static const char *literal = "<: :> <% %> %:";
static const char *spliced = TEXT(<\
:);
