# WebAssembly som mål: ny arch och ny platform i vcc

**Mål:** `vcc --target=wasm64-unknown-emscripten -c x.c` ger ett wasm-objekt som
`wasm-ld` länkar och som körs med samma resultat som clangs. `-S` ger LLVM:s
textsyntax för wasm. Slutmålet är att vcc kompilerar sig själv till wasm och att
explorer-sidan (`ccs/wasm/`) kör den versionen i stället för emscripten-bygget.

**Varför:** I dag är det emscripten (clang) som kompilerar vcc till wasm
(`ccs/wasm/build.sh`). Med en egen wasm-backend behövs clang inte för det, och vcc
får ett andra mål vid sidan av arm64. Det tvingar fram den uppdelning mellan
generiskt och målspecifikt som x86-64 också behöver (TODO.md: "Target-OS code out
of the generic files").

**Regler:** samma som fas 2. Clang är referensen (`--target=wasm64-unknown-emscripten`
och `wasm32-wasip1`), inget från clang/LLVM vid körning. `wasm-ld` och emscriptens
libc används för länkning och test tills steg W7. arm64-utdata får inte ändras av
något steg: varje commit verifieras mot de 1 326 utdata som användes vid
katalogomflyttningen (PLAN-fas2.md, "Status katalogstruktur").

## Utgångsläge (2026-11-09)

- Emittern genererar direkt från AST:t utan IR. Den generiska delen `emit/` är
  2 462 rader (kö, trådar, data, förpass, typer), arm64-delen `arch/a64/emit/` är
  10 292 rader. arm64-delen innehåller också sådant som inte är arm64-specifikt:
  konstantvikning under genereringen, nåbarhetsflaggan, inlining av
  `__always_inline`, `__builtin_constant_p`-regler och `need_fold`-omgenereringen.
- Typstorlekarna är en statisk LP64-tabell (`cc/type.c:6`), `__builtin_va_list` är
  AAPCS64-structen eller Apples `char *` (`cc/tu.c:94`).
- Predefines finns för två mål (`ppdata-predef.inc`: aarch64-linux, Darwin).
- Objektskrivarna är ELF (`platform/linux/elf.c`) och Mach-O
  (`platform/darwin/macho.c`) ovanpå den gemensamma objektmodellen `obj/obj.h`
  (sektioner, frags, symboler, fixups).
- Emscripten 7.0.0 finns (Homebrew), med en wasm64-sysroot (libc, compiler-rt).
  Node 26 kör memory64 utan flaggor. Clang 23 skriver wasm-`.s` och objekt för
  båda bredderna.

## Datamodellen: wasm64 först

| | aarch64-linux | wasm64-emscripten | wasm32-wasip1 |
|---|---|---|---|
| `long`, pekare, `size_t` | 8 | 8 | **4** |
| `long double` | binary128 | binary128 | binary128 |
| `char` | unsigned | **signed** | **signed** |
| `va_list` | AAPCS64-struct | `void *` | `void *` |
| största alignment | 16 | 16 | 16 |

wasm64 är LP64 som arm64, så typtabellen kan stå kvar. Det som skiljer är `char`
(som Darwin) och `va_list` (`void *`, Darwin har `char *`). Explorern är också
memory64. wasm32 (ILP32) kräver en typtabell per mål och 32-bitarspekare genom
hela emittern, så det kommer sist (W6).

## Arkitektur

| Del | Fil(er) | Innehåll |
|---|---|---|
| Målval | `driver/ppdriver.c`, `cc/`, `emit/` | Ett `target`-värde (cpu, os, pekarbredd) i stället för `darwin`-flaggan; `--target=wasm64-*`/`wasm32-*` |
| Predefines, `__has_*` | `pp/ppdata-predef.inc` via `gen-ppdata.sh` | En tabell till per mål, tagen från clang en gång |
| Layout | `cc/type.c`, `cc/layout.c` | `char` signed, `va_list` som `char *`; senare tabell per mål |
| Generisk kodgenerering | `emit/` | Det som lyfts ut ur `arch/a64/emit/`: vikning, nåbarhet, inlining, `__builtin_constant_p` |
| Kodgenerator | `arch/wasm/emit/` | AST → wasm-instruktioner: stackmaskin, locals, strukturerad kontrollflöde, skuggstack |
| Textutdata | `arch/wasm/emit/print.c` | `-S` i LLVM:s syntax (`.functype`, `.local`, `local.get`, `block`/`loop`/`end_block`) |
| Kodare | `arch/wasm/as/enc.c` | Opkoder, LEB128 (5/10 byte med utfyllnad där en relokering ska in) |
| Assembler | `arch/wasm/as/parse.c` | wasm-`.s` som `vas` läser: `-S` → `-c` går runt, och `.s`-tester mot llvm-mc |
| Fixups | `arch/wasm/obj/fixup.c`, `reloc.h` | `R_WASM_FUNCTION_INDEX_LEB`, `R_WASM_MEMORY_ADDR_LEB64`/`SLEB64`/`I64`, `R_WASM_TABLE_INDEX_*`, `R_WASM_GLOBAL_INDEX_LEB`, `R_WASM_TYPE_INDEX_LEB` |
| Objektskrivare | `platform/wasm/wasm.c` | Typ-, import-, funktions-, kod- och datasektioner; `linking` (symboltabell, segment, init-funktioner) och `reloc.CODE`/`reloc.DATA`. Objektformatet är detsamma för emscripten och WASI; skillnaderna mellan OS:en (libc, startkod, sjlj) ligger i drivern och ABI-koden |

### Kodgeneratorn

Att emittern arbetar från AST:t passar wasm bra, eftersom C:s strukturerade
satser blir wasm:s strukturerade kontrollflöde rakt av:

- `if`/`else` → `block`/`br_if` eller `if`/`else`/`end`; `while`/`for`/`do` → `block` + `loop`;
  `break`/`continue` → `br` till rätt djup.
- `switch` → `br_table` när fallen är täta (som arm64:s hopptabell), annars en
  jämförelsekedja. Hopp in i en loop (Duffs device) hanteras som `goto`.
- `goto`: i det vanliga fallet (framåt ut ur nästling, `goto out`/`goto err`) ett
  `block` runt hoppet. Annars en stackifier för funktionen: grundblock, dominatorträd
  och samma algoritm som LLVM:s `WebAssemblyCFGStackify`. Oreducerbar kontrollflöde
  blir en `loop` med en `br_table`-dispatcher på en etikettvariabel (LLVM:s
  `FixIrreducibleControlFlow`). Indirekt `goto` stöds inte, och clang avvisar det också.
- Värden lever på operandstacken inom ett uttryck. Variabler och temporärer blir
  wasm-locals (obegränsat antal: ingen registerallokering, `opt.c` behövs inte).
  Lokala variabler vars adress tas, arrayer och structs läggs på en skuggstack i
  linjärt minne via den globala `__stack_pointer`, som clang gör.
- Anrop: direkta `call`, indirekta `call_indirect` via tabellen med typindex.
  ABI som clangs `WebAssemblyABIInfo`: skalärer i parametrar; en struct med ett
  enda skalärt fält skickas som skalären, andra aggregat som pekare till en kopia;
  aggregat returneras via dold sret-pekare; varargs i en buffert på anroparens
  skuggstack, anroparen skickar pekaren.
- `__int128` och binary128 `long double` via compiler-rt-anrop (`__multi3`,
  `__addtf3`, …), som clang. Atomics som vanliga laddningar och lagringar utan
  delat minne, med `i64.atomic.*` under `-matomics`/`-pthread`.
- `setjmp`/`longjmp` som emscriptens wasm-EH-variant (`__wasm_setjmp`,
  `__wasm_setjmp_test`, `__wasm_longjmp`, `try_table`). vcc behöver det själv: pp och
  cc hoppar ut med `longjmp` vid fel.
- Inline asm: wasm-`.s`-fragment via `arch/wasm/as/parse.c`. Det är sällsynt i C för
  wasm och kan komma sent.

## Steg

| # | Leverans | Kriterium |
|---|----------|-----------|
| W0 | **Målval och front end.** `target` i stället för `darwin`; `--target=wasm64-unknown-emscripten`; predefines från clang; `char` signed och `va_list` som `void *` för wasm | `vpp -dM -E` = `clang -dM -E` för målet; `vcc-sema.py layout` med wasm64 som mål (layouter, konstanter, typer kontrollerade av clang); arm64-utdata byte-identiska |
| W1 | **Arch-krokar i `emit/`.** `do_item`, huvudet och svansen i `tu.c` och data-direktiven i `data.c` väljer per mål; wasm-generatorn viker själv med `cc_eval` (omplanerat: vikning och inlining lyfts inte ut ur `arch/a64/emit/` förrän wasm behöver dem) | arm64-utdata byte-identiska (`vcc-same.py`), `vbench.py check` |
| W2 | **Första wasm-utdata.** `arch/wasm/emit/` för heltalsaritmetik, locals, parametrar, `return`, direkta anrop, `if`/loopar/`break`/`continue`; `-S`. Explorern får målet "wasm64" | Utdata assembleras av `clang --target=wasm64-unknown-emscripten -c`, länkas av emcc och körs i node med samma resultat som clangs; nytt harness `bench/vcc-wasm.py` |
| W3 | **Minne och data.** Globala variabler, strängar, initierare, pekararitmetik, skuggstack för adresstagna lokala variabler, arrayer och structs, `memcpy`/`memset` för aggregat | `vcc-ctests.py --suite vcc` med `--target wasm64` i node |
| W4 | **Hela C.** Structs som värden (ABI ovan), varargs, `switch` med `br_table`, `goto` med stackifier, flyttal, bit-fält, VLA/`alloca`, `__int128` och `long double` via compiler-rt, atomics, `setjmp`/`longjmp` | gcc-torture och tcc-tests2 i node via emcc: samma godkända som clang; `bench/vcc-abi.py --target wasm64` (clang/vcc × anropare/anropad) |
| W5 | **Objektfiler.** `arch/wasm/as/enc.c`, `arch/wasm/obj/fixup.c`, `platform/wasm/wasm.c`; `-c` direkt utan text; `vas` läser wasm-`.s` | Länkas av `wasm-ld` och körs: allt från W4 med `-c`; kodare mot `llvm-mc` (`bench/vas-enc.py`); `wasm-objdump -x` jämförs mot clangs objekt för samma `.s` |
| W6 | **vcc kompilerar sig själv till wasm64.** NEON-lexern som skalär `VLEX_SCALAR`, CRC32C i mjukvara, `ccs/wasm/mman.c` som i dag; emcc länkar bara | `-S`-utdata från den självbyggda `vcc.wasm` byte-identisk med emscripten-byggets på testfilerna; explorern använder den |
| W7 | **wasm32 (ILP32).** Typtabell per mål, pekarbredd 4 genom emittern och objektskrivaren, wasi-libc | Samma sviter som W4 med `--target=wasm32-wasip1` |
| W8 | **Egen wasm-länkare** i `ld/` (valfritt; regeln om inget LLVM) | Ett program länkat av `vlink` körs som det länkat av `wasm-ld`; tider mot `wasm-ld` |

Ordningen gör att varje steg syns: W2 ger utdata i explorern och W5 körbara objekt.
W1 kan delvis gå parallellt med W0.

## Validering

- **Referens:** clang 23 med samma mål. `-S` jämförs inte rad för rad, eftersom
  instruktionsvalet skiljer sig. Det som jämförs är beteende (körning), ABI
  (anrop mellan clang- och vcc-objekt) och layout (`vcc-sema.py layout`).
- **Körning:** emcc länkar mot emscriptens wasm64-libc och node kör. Samma upplägg
  som `bench/vcc-ctests.py` med qemu för arm64. Cache som där: ett objekt
  byte-identiskt med ett som redan gått igenom körs inte igen.
- **arm64 oförändrat:** varje commit i W0, W1 och W7 verifieras mot de 1 326 utdata
  och `vbench.py gate`.

## Risker och öppna frågor

- **W1 är det svåra steget.** Vikning och inlining är sammanvävda med arm64:s
  registerhantering (`B_REG`, `fnctx.retreg`). Att lyfta ut dem utan att ändra en
  enda byte i arm64-utdata kräver små steg. Alternativet är att wasm-generatorn
  får en egen kopia av vikningen. Det går fortare men ger två ställen att hålla i
  takt med clangs regler, så det väljs bara om W1 drar ut på tiden.
- **`goto`-tung kod:** kärnan och vcc själv har mest `goto out`-mönster som klaras
  med ett omgivande `block`. Stackifiern behövs ändå för gcc-torture.
- **setjmp via wasm-EH** måste matcha emscriptens runtime exakt (symboler, taggar).
  Testas tidigt i W4 med ett litet program.
- **Prestanda:** wasm-utdata ska kosta ungefär som arm64-utdata per funktion (inga
  extra pass utöver stackifiern, som bara körs för funktioner med `goto`). Mäts med
  `vbench.py` när W5 är klart.

## Status

Före planen: wasm-*bygget* av vcc via emscripten (`ccs/wasm/`, commit 8e9f9da) och
explorer-sidan.

### W8 (2026-11-09): egen länkare, i gren `claude/wasm-w8-linker` (på W5)
- `ld/wasmld.c`: `vlink` länkar wasm när det anropas som `wasm-ld` eller med
  `-mwasm64`. Läser relokerbara wasm-objekt (clangs och vccs), arkiv (SysV-
  symboltabell, medlemmar hämtas lat av starka referenser, `--export` och
  `--export-if-defined` som i wasm-ld) och emscriptens stubbibliotek
  (`#STUB`: symboler som JS ger, med beroenden som blir rötter).
  Symbolupplösning (stark före svag, dubbletter är fel), GC från rötterna
  (exporter, `NO_STRIP`, konstruktorer), `__wasm_call_ctors` (init-funktioner
  efter prioritet), odefinierade svaga funktioner som stubbar som fäller och
  null i tabellen, syntetiska `__stack_pointer`, `__heap_base`, `__data_end`,
  `__stack_low/high`, `__start_/__stop_X`; minneslayout med `--stack-first`,
  `-z stack-size`, `--initial-heap`, `--no-growable-memory`, `--table-base`;
  segment slås ihop per namn (`.rodata`, `.data`, `.bss` utelämnas), data-
  symboler exporteras som oföränderliga globaler, relokeringar skrivs i de
  utfyllda fälten, `target_features` som unionen plus `--extra-features`.
- emcc använder den genom `EM_LLVM_ROOT` = en katalog med emscriptens LLVM-
  verktyg där `wasm-ld` pekar på `vlink` (`EM_FROZEN_CACHE=1`, cachen läses
  bara). `bench/vcc-wasm.py --vlink` länkar vccs sida så; `--obj --vlink` är
  hela vcc-kedjan (vpp -c, vlink) med bara emscriptens libc och JS kvar.
- Validering: `vcc-wasm.py -j 6 --vlink` och `--obj --vlink`: samma 1 590
  godkända som med wasm-ld (två fel hittade och rättade på vägen:
  `--export-if-defined` måste hämta arkivmedlemmar, annars saknas `fflush`
  och utdata utan radslut försvinner; odefinierad svag funktion ska ha
  tabellplats 0). Tid för emccs länk av t1.c (libc, dlmalloc, stubbar):
  `vlink` 5,4 ms mot `wasm-ld` 29,2 ms (bäst av 10, belastad maskin).
- Saknas (ger fel, inte tyst fel): COMDAT, TLS-segment i flertrådat läge,
  `--entry`/`_start`, `name`-sektionen (felsökningsnamn), `--growable-table`
  utan max, delade minnen, PIC/`-shared`. Utdata är inte byte för byte som
  wasm-ld:s (exportordning, segmentindelning); det krävs inte.

### W5 (2026-11-09): objektfiler, i gren `claude/wasm-w5-objects`
- `arch/wasm/as/as.c` (+ `was.h`, gränssnittet `wasmas.h`): LLVM:s wasm-`.s`
  (det generatorn skriver: `.functype`, `.globaltype`, `.tabletype`, `.local`,
  sektioner, `.globl`/`.weak`/`.hidden`/`.type`/`.size`/`.p2align`, `x = y`,
  `.set`, data-direktiv och strängar; hela MVP-instruktionsuppsättningen,
  sign-ext, `trunc_sat`, `memory.copy`/`fill`) till en modell; instruktioner
  kodas direkt, symboloperander lämnar fasta LEB-fält (5/10 byte) och
  relokeringar. `platform/wasm/wasm.c` lägger ut modellen som LLVM:s
  WasmObjectWriter: symboler, funktioner, importer och typer i den ordning
  assemblern registrerade symbolerna (`call_indirect`-signaturer där
  instruktionen står), utfyllda sektions- och delsektionsstorlekar,
  ELEM-segment och tabellimport när en funktionsadress tas,
  `linking` (symboltabell, segment) och `reloc.CODE`/`reloc.DATA`.
- `-c` för wasm64: `emit_tu` tar texten (som `VCC_OBJ_TEXT`) genom
  `wasm_assemble`; drivern skriver bytena (`cc_opts.wasm_obj`). Ännu genom
  text, inte direkt från generatorn (planens "utan text" återstår, en
  prestandafråga). `vasm --target=wasm64-…` läser wasm-`.s`.
- Validering: `bench/vcc-wasm.py --objcmp` jämför `vpp -c`:s objekt byte för
  byte med clangs objekt för samma `.s`: **alla identiska** (0 avvikelser på
  sviterna, 1 590 tester godkända länkade av emcc och körda i node på grenens
  bas 1856415, samma som `-S`-vägen). `--obj` kör sviterna med vccs objekt.
- arm64 oförändrat: `vcc-same.py` 13 167/13 167 mot bygget före wasm,
  `vcc-ctests` 1 743/1 743, bygget fil för fil utan varningar; emscripten-bygget
  (`ccs/wasm/build.sh`) tar med de nya filerna.
- Återstår i W5: generatorn direkt till modellen (utan text), `.init_array`
  (konstruktorer), TLS, egna sektioner, `vas` i vccd.

### W1, W2 (2026-11-09): klara
- W1 omplanerat: i stället för att lyfta ut vikning och inlining ur
  `arch/a64/emit/` (riskabelt, se Risker) väljer `emit/` generator per mål:
  `do_item` (funktioner till `wasm_emit_function`), huvudet (`wasm_header`:
  `.globaltype __stack_pointer`, `.tabletype`, en `.functype` per refererad
  funktion, sorterad och unik, ur varje tråds lista), svansen (ingen
  `.note.GNU-stack`) och `data.c` (en sektion per objekt, `.long`/`.quad`,
  strängar i `.rodata..L.str.N`). `ecx.w` håller wasm-tillståndet (en pool).
- W2: `arch/wasm/emit/` (`wimpl.h`, `wasm.c` funktioner/ABI/huvud, `wexpr.c`
  uttryck/lvärden, `wcall.c` anrop/varargs/inbyggda, `wstmt.c` satser/initierare).
  Ett värde per C-uttryck på operandstacken; skalära lokala variabler utan tagen
  adress (prepassets `lnoreg`) i wasm-locals, resten i en ram på skuggstacken
  (`__stack_pointer`, återställd från en sparad local); `return` som `br` ut ur
  funktionens yttersta block med värdet i en local. ABI som clang: en struct med
  ett element som skalären, tomma ignoreras, andra som pekare till en kopia,
  sret; `__int128`/`long double` som två i64; varargs i en buffert på anroparens
  ram (justering enligt typen, steg avrundat till 4, som `emitVoidPtrVAArg`).
  `main` byter namn som hos clang (`__main_argc_argv`, eller `__original_main`
  plus omslaget `main(i32, i64)` och `__main_void`). Implicit deklarerade
  biblioteksfunktioner får bibliotekets prototyp (nytt `cc_lib_sig`, bara uppslag);
  anrop till en K&R-definition i samma fil använder definitionens parametrar.
  `switch` med `br_table` (täta fall) eller jämförelsekedja, fallen på
  toppnivån i kroppen. Med i W2 redan: globala data, strängar, initierare,
  pekararitmetik, bit-fält, structs som värden, varargs, `alloca`, flyttal.
- Explorern har målet `wasm64-unknown-emscripten` (vcc i wasm som ger wasm).
- Nytt `bench/vcc-wasm.py`: sviterna från `vcc-ctests.py` byggda av clang och av
  vcc (`-S`, assemblerat av clang) för wasm64, länkade av emcc, körda i node.
  LLVM:s assembler typkontrollerar `call_indirect`-index mot en i32-tabell (clangs
  egen `-S` faller likadant), så en fil vars enda fel är det assembleras om med
  `-Wa,--no-type-check`. Resultat: gcc 1 411/1 605, tcc 85/104, vcc 22/30
  (alla 1 518/1 739, 87,3 %). Det som återstår (W4): `goto` (44), inline asm (36),
  vektorer (36), `__int128`/`long double` (31), VLA (14), atomics och några
  inbyggda, cleanup, Duffs device.
- arm64 oförändrat: `vcc-same.py` 13 160/13 160 mot bygget före, `vbench.py
  check --quick` 66/66; `vcc-ctests --suite vcc` 34/34, `vcc-sema batch` 878/879,
  bygget fil för fil (`VCC_UNITY=`) utan varningar.

### W0 (2026-11-09): klar
- `--target=wasm64-…` ger `PP_WASM64` (tredje kolumnen i `has_tab`, `pp.h`); drivern
  skiljer `lnx` (aarch64 Linux) från Darwin och wasm, så Linux-tilläggen (`-fshort-wchar`-
  tabellen, `__ARM_NEON`, LSE) inte hamnar i wasm-predefines. `gen-ppdata.sh` ger
  `predef_wasm64`, `predef_order_wasm64` och wasm-svaren på `__has_*`; körd med
  `EXTRA_INC="work/glibc-include <emscriptens sysroot/include>"`. Linux- och
  Darwin-kolumnerna och -tabellerna är oförändrade; 23 nya namn ur emscriptens headers.
- Front end för wasm64, kontrollerat mot clang 23: `char` signed, `wchar_t` `int`,
  `__builtin_va_list` `void *`, `long double` binary128 med alignment 8 (emscripten),
  namnlösa bit-fält som Apple (påverkar inte postens alignment), GNU-vektorer
  alignade till hela storleken (arm64: högst 16).
- `-S`/`-c` för wasm64 ger ett fel som pekar på W2 tills kodgeneratorn finns.
- Verifierat: `vpp -dM -E` = clang för wasm64 med `-O0/-O1/-O2/-Os`, `-std=c99`,
  `-fPIC`, `-ffreestanding` (C23 avviker, som för Linux redan före);
  `vcc-bflayout.py` 6 604 påståenden per mål, 0 avvisade för alla tre målen;
  layoutpåståenden (`--vcc-layout-checks`) för 1 880 testfiler med wasm64 som mål:
  83 858 påståenden, de 9 filer clang avvisar har samma avvisningar för aarch64-linux
  (befintliga, `_Atomic`-kompatibilitet m.m.). arm64 oförändrat: nytt
  `bench/vcc-same.py` 13 160/13 160 utdata identiska (1 880 filer × -c -O0/-O1/-O2,
  -S, -E, Darwin -c/-S) mot bygget före, `vbench.py check --quick` 66/66;
  `vcc-diff batch --cmp --kernel` 873/873, `vcc-sema batch` 878/879, `vcc-driver`
  41/41, `vcc-ctests --suite vcc` 34/34.
