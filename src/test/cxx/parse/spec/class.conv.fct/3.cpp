// [class.conv.fct] Example 3 (paragraph 3), docs/cxx-spec/class/class.conv.fct.md
// needs: P5
// status: annotated

&ac.operator int*i; // syntax error: // syntax-error
                    // parsed as: &(ac.operator int *)i
                    // not as: &(ac.operator int)*i
