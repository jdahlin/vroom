/* vcc-flags: -std=c2y -pedantic-errors */
int f(int x){switch(x){case 2 ... 5:return 1;default:return 0;}}
int main(void){return f(1)||!f(2)||!f(5)||f(6);}
