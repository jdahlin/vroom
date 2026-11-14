int t(void){return 3;}
int al(void) __attribute__((alias("t")));
int u(void){return al();}
int (*p(void))(void){return al;}
