// llvm-test-suite SingleSource/Regression/C++/global_type.cpp (Apache-2.0
// WITH LLVM-exception): globals initialized by address constants
// This testcase was causing an incorrect type emission for data.
double data[100];

double *dpa = data;
double *dpb = data+100;

int main() { return 0; }
