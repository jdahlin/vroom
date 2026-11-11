#include "types.h"
int main(void){B b={};b[64]=1;B r=fixed(b,19);return !r[0]||!r[64];}
