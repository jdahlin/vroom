# Onboarding diagnostic baseline

Captured 2026-11-11 from the fixed compiler snapshot identified below. These are
observations, not replacement expectations: `manifest.json` retains the independent
message contracts. `XFAIL` means a documented diagnostic gap, not a covered case.

Compiler SHA256: `f39c5f928f9d38c64c058c17cce65d5bcd20bd61831b4fcf882b07394d36920b`.

| Case | x86 | ARM | Story |
|---|---|---|---|
| B01 | PASS | PASS | Missing declaration semicolon |
| B02 | PASS | PASS | Missing closing parenthesis |
| B03 | PASS | PASS | Misspelled visible variable |
| B04 | PASS | PASS | Library call missing its declaration |
| B05 | PASS | PASS | Declared function missing from final link |
| B06 | PASS | PASS | Assignment where comparison may be intended |
| B07 | XFAIL | XFAIL | Empty if body before apparently conditional statement |
| B08 | XFAIL | XFAIL | Indentation suggests two controlled statements |
| B09 | XFAIL | XFAIL | Chained comparison does not test a mathematical range |
| B10 | XFAIL | XFAIL | Integer quotient converted to floating point too late |
| B11 | PASS | PASS | Value is missing on one branch |
| B12 | PASS | PASS | Non-void function misses a return path |
| B13 | XFAIL | XFAIL | Numeric input receives a value instead of an address |
| B14 | XFAIL | PASS | Input and output formats must match argument types |
| B15 | XFAIL | XFAIL | Input result is ignored before using the destination |
| B16 | XFAIL | XFAIL | String input has no bound for a known buffer |
| B17 | XFAIL | XFAIL | Character input follows numeric input without a whitespace policy |
| B18 | XFAIL | XFAIL | Pointer-size calculation mistaken for element count |
| B19 | PASS | PASS | Read past the last array element |
| B20 | XFAIL | XFAIL | String address comparison mistaken for text comparison |
| B21 | XFAIL | XFAIL | String use requires space for its terminator |
| B22 | XFAIL | XFAIL | Pointer declaration creates no pointed-to object |
| B23 | XFAIL | XFAIL | Allocation uses pointer size instead of object size |
| B24 | PASS | PASS | Returned address refers to an expired local object |
| B25 | XFAIL | XFAIL | Read through a pointer after freeing its object |

## B01: Missing declaration semicolon

```text
01_missing_semicolon/broken.c:2:14: error: expected ';' at end of declaration
    2 |     int n = 3
      |              ^
      |              ;
1 error generated.
```

## B02: Missing closing parenthesis

```text
02_unmatched_delimiter/broken.c:2:11: error: expected ')'
    2 |     if (1 { return 0; }
      |        -  ^
      |        |
      |        to match this '('
02_unmatched_delimiter/broken.c:2:8: note: to match this '('
1 error generated.
```

## B03: Misspelled visible variable

```text
03_misspelled_identifier/broken.c:3:12: error: use of undeclared identifier 'coutn'
    3 |     return coutn != 3;
      |            ^~~~~
1 error generated.
```

## B04: Library call missing its declaration

```text
04_missing_header/broken.c:2:5: error: call to undeclared library function 'puts' with type 'int (const char *)'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
    2 |     puts("Hello");
      |     ^
1 error generated.
```

## B05: Declared function missing from final link

```text
ld.lld: error: undefined symbol: answer
>>> referenced by <temporary>:(main)
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```

## B06: Assignment where comparison may be intended

```text
06_assignment_condition/broken.c:3:11: warning: using the result of an assignment as a condition without parentheses [-Wparentheses]
    3 |     if (n = 3) return 0;
      |         ~~^~~
06_assignment_condition/broken.c:3:11: note: place parentheses around the assignment to silence this warning
    3 |     if (n = 3) return 0;
      |           ^
      |         (    )
06_assignment_condition/broken.c:3:11: note: use '==' to turn this assignment into an equality comparison
    3 |     if (n = 3) return 0;
      |           ^
      |           ==
1 warning generated.
```

## B07: Empty if body before apparently conditional statement

```text
(no diagnostic emitted)
```

Missing contract: empty.*(body|if)|if.*empty|misleading.*indent.

## B08: Indentation suggests two controlled statements

```text
(no diagnostic emitted)
```

Missing contract: misleading.*indent|indent.*(guard|if)|not guard.

## B09: Chained comparison does not test a mathematical range

```text
(no diagnostic emitted)
```

Missing contract: comparisons?.*(mathematical|meaning|constant|always)|comparison.*(bool|boolean)|chained.

## B10: Integer quotient converted to floating point too late

```text
(no diagnostic emitted)
```

Missing contract: integer division|integer.*quotient|division.*before.*(float|double|conversion).

## B11: Value is missing on one branch

```text
11_uninitialized_read/broken.c:3:9: warning: variable 'n' is used uninitialized whenever 'if' condition is false [-Wsometimes-uninitialized]
    3 |     if (ready) n = 1;
      |     ----^~~~~--
      |     |
      |     remove the 'if' if its condition is always true
    4 |     return n;
      |            - uninitialized use occurs here
11_uninitialized_read/broken.c:4:12: note: uninitialized use occurs here
11_uninitialized_read/broken.c:3:5: note: remove the 'if' if its condition is always true
11_uninitialized_read/broken.c:2:10: note: initialize the variable 'n' to silence this warning
    2 |     int n;
      |          ^
      |           = 0
1 warning generated.
```

## B12: Non-void function misses a return path

```text
12_missing_return/broken.c:3:1: warning: non-void function does not return a value in all control paths [-Wreturn-type]
    2 |     if (ready) return 1;
      |         ~~~~~ (1) 'ready' is false
    3 | }
      | ^ (2) 'choose' ends here without a return
      = help: 'choose' returns 'int': end it with a return statement
1 warning generated.
```

## B13: Numeric input receives a value instead of an address

```text
(no diagnostic emitted)
```

Missing contract: format.*int.*\*.*(argument|type)|expects?.*(int.*\*|pointer|address)|scanf.*address.

## B14: Input and output formats must match argument types

```text
(no diagnostic emitted)
```

Missing contract: format.*(int|%d).*(long|argument)|expects?.*int.*long.

ARM already reports:
```text
14_format_type/broken.c:4:18: warning: format specifies type 'int' but the argument has type 'long' [-Wformat]
    4 |     printf("%d", count);
      |             ~~   ^~~~~
      |             %ld
1 warning generated.
```

## B15: Input result is ignored before using the destination

```text
(no diagnostic emitted)
```

Missing contract: (scanf|input|conversion).*(result|fail|return)|ignoring return value.*scanf|unchecked.*(input|scanf).

## B16: String input has no bound for a known buffer

```text
(no diagnostic emitted)
```

Missing contract: (unbounded|width|bound|overflow|buffer).*(scanf|%s|string|input)|scanf.*(bound|width|overflow)|%s.*(bound|width).

## B17: Character input follows numeric input without a whitespace policy

```text
(no diagnostic emitted)
```

Missing contract: (newline|white.?space|remaining.*character|leftover).*|%c.*(consume|skip).

## B18: Pointer-size calculation mistaken for element count

```text
(no diagnostic emitted)
```

Missing contract: sizeof.*(pointer|array.*parameter)|pointer.*(size|array).

## B19: Read past the last array element

```text
19_array_off_by_one/broken.c:3:12: warning: array index 3 is past the end of the array (that has type 'int[3]') [-Warray-bounds]
    2 |     int a[3] = {0};
      |     - array 'a' declared here
    3 |     return a[3];
      |            ^ ~
      +-----+-----+-----+ +-----+
      | [0] | [1] | [2] | | [3] |
      +-----+-----+-----+ +-----+
      'a': 3 x 'int', 12 bytes
                             ^ index 3: bytes 12..15, just past the end
19_array_off_by_one/broken.c:2:5: note: array 'a' declared here
1 warning generated.
```

## B20: String address comparison mistaken for text comparison

```text
(no diagnostic emitted)
```

Missing contract: string.*(compar|address|literal)|comparison.*string|compar.*unspecified.

## B21: String use requires space for its terminator

```text
(no diagnostic emitted)
```

Missing contract: (terminat|nul|initializer.string.*too long|nonstring).

## B22: Pointer declaration creates no pointed-to object

```text
(no diagnostic emitted)
```

Missing contract: uninitialized|uninitialised|pointer.*(assign|initializ).

## B23: Allocation uses pointer size instead of object size

```text
(no diagnostic emitted)
```

Missing contract: sizeof.*pointer|allocation.*(size|small)|allocat.*(enough|insufficient)|bounds|outside.

## B24: Returned address refers to an expired local object

```text
24_return_local_address/broken.c:3:13: warning: address of stack memory associated with local variable 'n' returned [-Wreturn-stack-address]
    3 |     return &n;
      |             ^
1 warning generated.
```

## B25: Read through a pointer after freeing its object

```text
(no diagnostic emitted)
```

Missing contract: use.after.free|used after.*free|freed.*(memory|object|pointer)|deallocated.
