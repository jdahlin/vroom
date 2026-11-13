static _Complex double selected = (1. ? 3.+4.i : 7.+8.i) * (2.-1.i);
static _Complex long double inverse = (0. ? 2.L+3.Li : 4.L+2.Li) / (2.L+0.Li);
static _Complex double logical = ((2.i != 3.i) + (1 ? 2.i : 3.i)) / 1.i;
static int calls;
static int yes(void){calls++;return 1;}
static _Complex double value(void){calls++;return 3.+4.i;}
int main(void) {
 if(__real__ selected!=10.||__imag__ selected!=5.)return 1;
 if(__real__ logical!=2.||__imag__ logical!=-1.)return 4;
 if(__real__ inverse!=2.L||__imag__ inverse!=1.L)return 2;
 _Complex double runtime=(yes()?value():0.)*(2.-1.i);
 if(calls!=2||__real__ runtime!=10.||__imag__ runtime!=5.)return 3;
 return 0;
}
