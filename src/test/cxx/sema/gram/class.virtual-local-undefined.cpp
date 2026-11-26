/* [class.local], [class.virtual]: a local class's virtual function that is
   declared and never defined is no key function: the translation unit that
   uses the class emits its virtual table and type information */
void test()
{
    struct S {
        virtual void f();
    };
    S s;
}
