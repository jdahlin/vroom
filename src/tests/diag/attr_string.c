/* a non-string argument to a string attribute names the attribute, and no "expected ')'" follows */
int x __attribute__((section(1)));
int y __attribute__((__alias__(2)));
