/* An enumeration whose definition analysis gave up in (a lambda in an
   initializer) is reported at that construct; a later use of an enumerator
   is not a false "shall be followed by the name of an enumerator". */
enum class M { kN, kL = [] { return 1; }(), kLast };
int c = (int)M::kLast;
