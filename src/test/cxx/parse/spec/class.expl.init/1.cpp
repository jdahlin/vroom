// [class.expl.init] Example 1 (paragraph 1), docs/cxx-spec/class/class.expl.init.md
// needs: P5
// status: well-formed (as far as the example says)

struct complex {
  complex();
  complex(double);
  complex(double,double);
};

complex sqrt(complex,complex);

complex a(1);                   // initialized by calling complex(double) with argument 1
complex b = a;                  // initialized as a copy of a
complex c = complex(1,2);       // initialized by calling complex(double,double) with arguments 1 and 2
complex d = sqrt(b,c);          // initialized by calling sqrt(complex,complex) with d as its result object
complex e;                      // initialized by calling complex()
complex f = 3;                  // initialized by calling complex(double) with argument 3
complex g = { 1, 2 };           // initialized by calling complex(double, double) with arguments 1 and 2
