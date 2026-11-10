int f1(int c){int x; if(c) x=1; else return x+1; return x;}
int f2(int c){int x; if(c) goto l; x=1; return x; l: return x;}
int f3(int c){int x; for(;;){ if(c){x=1;break;} return x;} return x;}
int f4(int c){int x; c ? (x=1) : 0; if(!c) return x; return 0;}
int f5(int c){int x; switch(c){case 0: x=1; case 1: return x;} return 0;}
int f6(int c){int x; switch(c){ x=1; case 1: return x;} return 0;}
