# vcc --explain pages

One page per diagnostic id (`-fdiagnostics-show-id` shows a diagnostic's id; it is clang's
name for the diagnostic or a `vcc_*` one).  `vcc --explain ID` prints the page;
`src/tools/gen-explain.py` compiles the pages into `src/driver/explain.inc` (no files are
read at run time), and `bench/vcc-explain.py` checks every page: its `c wrong` example must
give exactly that id, its `c fixed` example must not, and must compile without errors.

A page is Markdown: a `#` title (the message in words), the cause and the fix in prose,
then the two examples.  A line `<!-- flags: ... -->` gives both examples' options.  Write
a page when the explanation adds something the message cannot say (rustc's rule).
