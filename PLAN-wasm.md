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

### W9 (2026-11-11): ersätt Emscripten, första fristående testvägen klar

Målet är att bygga och köra wasm utan emcc/emsdk. Emscripten är i dag
test-/runtimeinfrastruktur, inte en nödvändig del av vcc:s wasm-kodgenerator.
Vcc har redan assembler, objektskrivare och `ld/wasmld.c`; W8 ersätter
länkaren men den gamla testvägens emcc väljer fortfarande bibliotek och
genererar JS. `wasm*-unknown-emscripten` anger nuvarande mål-ABI, inte att
programmet måste byggas eller startas med Emscripten.

| Steg | Leverans | Status och verifiering |
| --- | --- | --- |
| W9.1 | Libc-fri testväg: vpp -S för kodkrav, vpp -c för eget objekt, vlink för modul, liten Node-launcher | Klar för OPT1:s första regel: `bench/vcc-opt-rules.py --target wasm32-unknown-emscripten --run` och motsvarande wasm64. O0–O3 kör samma C-regression. Ingen emcc, LLVM-assembler, wasm-ld eller SDK används i denna wasm-väg. Verifierat med systemets `/usr/bin/node` 24.22.2 och därefter 25.9.0 utan SDK-sökväg. Tidigare Node 22 saknade table64 och kunde inte köra denna wasm64-modul. |
| W9.2 | Egen test-runtime för ett uttryckligt urval av de vendrade C-sviterna | Planerad. Återanvänd portabel C ur `tests/rt/rt.c` eller granskade libc/compiler-rt-källor; bygg med vcc. Separera arm64-start, UART/exit- och FP-asm. Lägg små JS-importer för output/exit samt heap med memory.grow. Behåll samma deklarationer/testkontrakt och verifiera även referenskompilatorns program mot runtime. |
| W9.3 | Komplett bibliotek-/builtinsstöd för testkorpuset, inklusive exceptionbaserad setjmp | Planerad. Inventera faktiska undefined-symboler; implementera eller vcc-bygg kompatibla __int128/binary128/complex-hjälpare och libc/libm-källor. __wasm_setjmp/__wasm_longjmp och __c_longjmp-taggen kräver egna ABI-/EH-tester. Saknade funktioner ger uttryckligt unsupported, inte falskt godkända eller tyst filtrerade tester. |
| W9.4 | Fristående explorer och självbygge | Planerad. Ersätt em-config/sysroot-val, libc-header/runtimeleverans, startup/argv/stdout/exit och genererade VccModule/FS/callMain/ExitStatus med egen adapter. Packa filerna i VFS; behåll worker-API eller migrera worker/selfrun i samma steg. Testa filer, minnestillväxt, diagnostik och självbyggets stage2=stage3. |
| W9.5 | GHA:s obligatoriska wasm-grind använder den egna kedjan | Påbörjad med W9.1-regressionerna i `ci/premerge.sh`; den gamla fullsviten behöver fortfarande emcc. Ta bort SDK-installationen först när samma beslutade korpus och ABI-tester går med W9.2–W9.4. Emscripten kan därefter finnas som valfri jämförelsereferens, aldrig som obligatorisk bygg-/runtimekomponent. |

Det krävs inte en nyskriven full libc. Valda befintliga källor kan byggas
av vcc, med dokumenterat ursprung/licens och testad wasm32/64-ABI. Den
generiska optimeraren väntar inte på detta runtimearbete: W9.1 ger redan
egen kedja för dess positiva kodkrav och sidoeffekts-/gränsvärdestester.
Installation på värden och aktivering av emsdk är inte en produktleverans.

Kodinventering: `bench/vcc-wasm.py` använder emcc för `rt/wasm.c` och
slutlänk/JS; `rt/wasm.c` är bara assert-adaptern mot extern libc.
`ccs/wasm/build.sh` använder Emscriptens sysroot och JS-fil-/processglue.
`explorer/worker.js` och `selfrun.mjs` använder VccModule, FS och callMain.
`ccs/wasm/mman.c` äger redan reservationspolicyn men behöver sbrk, pread,
errno och standardbiblioteksfunktioner. Wasm-generatorn emitterar
compiler-rt-anrop och EH-ramar men levererar ännu inte deras runtime.

### Gemensamma optimeringar (2026-11-11): planerade för wasm32 och wasm64

`PLAN-fas2.md`, **Gemensam optimerare utan SSA**, OPT0–OPT8 äger den nya
optimeringsplanen. Wasm är obligatorisk konsument av gemensamma C-regler
och strategier tillsammans med x86 och arm64. Anslutningar finns i
`wexpr.c` (`wbinary`/`warith`, `wknown`, `wlocal`, `wvlanes`), `wstmt.c`
(`wswitch`, nåbarhet) och `wcall.c:wcall`. Behåll operandstack, wasm-lokaler,
strukturerade resultatblock och `br_table`; inga fysiska registerpass krävs.
Gemensam inliningpolicy måste kompletteras med verkliga wasm-bindningar
och retur-/goto-/cleanup-remappning. Native-only verktyg eller en x86-grön
audit räknas inte som wasm-verifiering. Kör båda pekarbredderna, med
scalar-fallback utan SIMD/tail-call-features. Rena extraktioner skall ge
samma utdata; nya regler får avsedda, testade förbättringar på wasm också.
Ingen regel implementerad i detta steg; befintliga W-statusar ändras inte.

Före planen: wasm-*bygget* av vcc via emscripten (`ccs/wasm/`, commit 8e9f9da) och
explorer-sidan.

### Integrering (2026-11-09): gren `claude/wasm`
Allt färdigt i en gren: W5 och W8 (`claude/wasm-w5-objects`,
`claude/wasm-w8-linker`), W7:s front end (`claude/wasm-w7-wasm32`) och resten
av W4 (`claude/wasm-compiler-explorer-5ed0bf`). Inte med: cleanup-attributet
(`claude/wasm-cleanup-wip`, ett oförklarat körfel). Konflikter bara i
`emitimpl.h` (`WASM()` täcker nu wasm32, plus `inline_only`) och
`bench/vcc-wasm.py` (`rt/wasm.c` länkas också med `--vlink`).
- `vcc-wasm.py -j 6`: 1 717/1 742 (gcc 1 588/1 605, tcc 97/104, vcc 32/33).
- arm64: `vcc-same.py` 13 165/13 181; de 16 avvikelserna är avsedda
  (`ctor_priority.c`: `.init_array.N` som clang, W5; `36_array_initialisers.c`
  och `composite_array.c`: storleken från en tidigare deklaration, rättelse i
  front end). `vcc-ctests` 1 745/1 745, `vcc-sema batch` 878/879, bygget fil
  för fil utan varningar.

### W4 (2026-11-09): nästan klar
Gjort sedan W2: `goto` (dispatch-loopar med tillståndslocals och `br_table`,
hopp in i nästlade satser), etikettadresser och beräknat `goto`, inline asm
med tom mall, vektorer (lane för lane, sret), `__int128`/`long double` via
compiler-rt (16-byte-värden som adresser till platser), VLA (under
`__stack_pointer`, återlämnas vid blockslut, `break`/`continue` och bakåthopp),
`alloca`, atomics som vanliga laddningar/lagringar (som clang utan
atomics-funktionen), överspillsinbyggda, död kod efter hopp och kända villkor,
lokala kopior av inline-only-funktioner (emittern inlinar inte), funktions-
adresser i data (`.int32`/`.int64`, tabellindex), varargs-platser minst 4 byte.
Återstår (fel, inte tyst fel):
- `emit` 13: cleanup-attributet (2, WIP-gren), `asm goto` (2), `goto` till en
  etikett som inte står i en sammansatt sats (2), `case` i en nästlad sats
  (1), `switch` på 128 bitar (1), överspillsinbyggda på `__int128` (1),
  `_Atomic`-objekt (1), bit-fält över 8 byte (1), `atomic_signal_fence` (1,
  front end), destruktorer (1, W5).
- `link` 6: testerna förutsätter att optimeringen tar bort `link_error`-anrop.
- `run` 6: optimeringsberoende (`bcp-1`, `builtin-constant`,
  `frame-address`, `20030125-1`), `complex-6` (`long double _Complex`) och
  `24_math_library` (bibliotekets signaturer för implicit deklarerade).
- `bench/vcc-abi.py --target wasm64|wasm32 [--vlink]` (2026-11-10): de fyra
  kombinationerna clang/vcc × anropare/anropad, `vpp -c`, länkade av emcc och
  körda i node.  Nytt test `tests/abi/aggregates` (poster 0..48 byte, ett
  element, unioner, bit-fält, `__int128`, `long double`, `_Complex`, varargs
  med poster, återanrop åt båda hållen) och `aligned_args`.  Det hittade:
  en vektor med en lane returneras som skalär (som LLVM), inte genom pekare.
  wasm64 9/9, wasm32 8/8 (`bitfield_layout` kräver LP64), `--vlink` 9/9.
  Samma tester hittade fyra arm64-fel (rättade): Linux rundar till ett jämnt
  registerpar efter clangs "unadjusted" justering (inte postens egen
  `aligned()`), Apple rundar aldrig; Apples `long double` (8 byte) i HFA;
  Apples HFA på stacken med elementets justering och storlek.

Senare samma dag: `setjmp`/`longjmp` genom wasm-EH (`-mllvm -wasm-enable-sjlj`,
som emscriptens `-sSUPPORT_LONGJMP=wasm`: `__wasm_setjmp`/`__wasm_longjmp`,
`try`/`catch __c_longjmp`, taggar i assemblern, objektet och vlink;
`tests/vcc/setjmp_sites.c`), destruktorer, `asm goto` med tom mall, `switch`
på `__int128`, breda bit-fält, `_Atomic`-objekt, matematikfunktioner som
instruktioner. `vcc-wasm.py -j 6`: 1 739/1 746 i alla lägen (`-S`, `--objcmp`
byte för byte, `--obj --vlink`); kvar är de 6 `link_error`-testerna (kräver
optimering) och `bcp-1`.

### W6 (2026-11-10): klar, explorern inte bytt
vcc kompilerar sig själv till wasm64: `VCC=build/ccs/vpp OUT=../build/wasm-self
sh ccs/wasm/build.sh` (från `ccs/`) gör varje objekt med vcc:s wasm-generator,
assembler och objektskrivare mot emscriptens libc-huvuden; emcc länkar bara
(`-sSUPPORT_LONGJMP=wasm`, vcc:s setjmp är wasm-EH).  Det som behövdes:
- vcc:s `<arm_neon.h>`/`<arm_acle.h>` har C-versioner av inline-asm-
  intrinsics för andra mål än arm64 (lane för lane; CRC32/CRC32C en nibble i
  taget), provade mot clangs NEON på 20 000 slumpade indata;
- `_Thread_local` som vanliga objekt på wasm, som LLVM gör utan delat minne
  (`tests/vcc/thread_local.c`);
- `__builtin_bitreverse8..64`, `__builtin_isinf`/`isfinite` i wasm-generatorn
  (`tests/vcc/bitreverse.c`, `fp_class_builtins.c`).
`bench/vcc-self.py -j 6`: varje test i sviterna, förbehandlat av den native
vpp för aarch64 och wasm64, kompileras `-O2 -S` av både emcc-bygget och det
självbyggda `vcc.wasm` (`ccs/wasm/selfrun.mjs`, node): 3 741/3 741 utdata
byte-identiska (3 428 kompilerade, resten samma diagnostik), 37 s.
`vcc.wasm` 1,26 MB mot emcc:s 1,61 MB, men ~7x långsammare (9,0 s mot 1,2 s
på 41 MB `many_probes`): ingen optimering, intrinsics lane för lane.
Explorern använder fortfarande emcc-bygget; att byta är ett beslut (hastighet)
och en ny publicering av sidan.

### W7 (2026-11-09): klar
`--target=wasm32-unknown-emscripten`: predefines som clang, ILP32 i front end,
och nu hela vägen: pekarbredden per mål i generatorn (`WPT(c)`, `wrep()`:
`i32`-adressering, `__stack_pointer` och tabellindex som i32, `.int32`-
adresser i data, compiler-rt-signaturer med `P`), sjlj-nyttolasten
`{env, val}` på 4+4 byte, assemblern med `R_MEMORY_ADDR_LEB` (5 byte) för
symboler i memarg, objektskrivaren med i32-gränser för minne och tabell och
`i32.const`-offset, och `vlink` som läser bredden ur objektens minnesimport
(fel vid blandning).  Två rättelser i front end som wasm32 hittade (ILP32 gör
`1ul << 40` till ett överskift): konstanta skift viks som clangs C (antal ≥
bredd blir bredd−1, negativt antal skiftar åt andra hållet,
`tests/vcc/const_shift.c`), och `-Wno-[error=]int-conversion`,
`incompatible-pointer-types` och `incompatible-function-pointer-types` gör
felet till en varning som clang.
- `vcc-wasm.py -j 6 --wasm32`: 1 739/1 746, `--objcmp` byte för byte med
  clangs objekt, `--obj --vlink` (`build/ccs/wasmtests32`); samma 7 kvar som
  wasm64.

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
- Tillägg (i `claude/wasm-w8-linker`): konstruktorer. Generatorn tappade
  `__attribute__((constructor))` tyst för wasm; nu `.init_array[.N]`, som
  assemblern gör till objektets init-funktioner (linking-delsektion 6, i
  sektionernas ordning som LLVM); prioriteten `constructor(N)` sparas i
  `attrs.prio` (front end tappade den, även för arm64, som nu skriver
  `.init_array.N` som clang). Assemblern tar också egna sektioner
  (`.custom_section.X`; `producers` och `target_features` sist),
  `.no_dead_strip` (tabellimporten clang -O0 tvingar fram), segmentflaggan
  `"S"` och symboler som minnesoffset (`i32.store x`, MEMORY_ADDR_LEB64):
  clangs egen `-O0`-`.s` ger nu samma objekt som clang. Test
  `tests/vcc/ctor_priority.c` (arm64 och wasm); destruktorer ger fel för
  wasm (clang gör dem till `__cxa_atexit`-anrop).
  Namn utanför ASCII skrivs inom citattecken (`"привет"`, sektionen
  `".data.привет"`) som clang -S; LLVM:s wasm-assembler tar inte tillbaka
  dem (clangs egen `.s` faller också), så `vcc-wasm.py` bygger då vccs sida
  med `vpp -c` (tcc/83_utf8_in_identifiers godkänt).
- Återstår i W5: generatorn direkt till modellen (utan text), destruktorer,
  TLS, `vas` i vccd.

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

## Referenskontrakt för GCC-körtesterna

`bench/vcc-wasm.py` respekterar GCC:s uttryckliga, ovillkorliga
`dg-require-effective-target label_values` före kompilering: LLVM:s Wasm-mål
kan inte representera GNU-etikettadresser som lämnar funktionen eller lagras i
data. Dessa fall redovisas som `skip:unsupported-target`, inte som godkända test.
Att en enklare etikettadress kan optimeras bort innebär inte stöd för kravet.
Övriga målvillkor, exempelvis `indirect_jumps`, omfattas inte av denna regel.

Verifierat med LLVM 23 på `20071220-1`, `20071220-2` (etikettadress i statisk
tabell) och `990208-1` (etikettadress lämnar inline-funktionen). Utan
målvillkorskontrollen ger LLVM fatala backendfel med stackdump. Dessa fel är
fortfarande `error:oracle-compile` för test utan detta uttryckliga källkontrakt;
diagnostiktext får aldrig omklassificera en krasch, timeout eller verktygsfel
till en skip. `bench/test-runtime-oracle.py` kontrollerar båda vägarna.
Oväntade `fail` och `error` skriver alltid svit/testnamn även utan `-v`.

### OPT5: typade ramcellsoperationer (pågående)

Wasm delar nu den exakta cell-/överlappsanalysen (`emit/rec/slots.c`) och
värdetillgängligheten (`emit/rec/slot-values.c`) med arm64. Den separata
adaptern `arch/wasm/emit/slots.c` registrerar typade load/store-, överlapps-
och kontrollhändelser när kroppen produceras. Optimeraren väljer handlingar
före renderingen; ingen assemblertext läses eller mönstermatchas. Stabila
allokerings-ID:n följer direkta ramobjekt och deras structfält. Endast utvalda
vidarebefordringar får egna wasm-locals, som inte återanvänds av temporärpoolen.

Första delen ersätter exakta 4/8-bytes heltals- och flyttalsladdningar samt
tar bort överskrivna butiker inom raka kodsegment vid O1–O3. Operanderna
utvärderas fortfarande: en laddning blir `drop; local.get`, en borttagen
butik konsumerar fortfarande adress och högerled. Överlapp, volatile och
atomic utesluts via samma cellanalys; okända minnesoperationer, anrop, asm
och strukturerade kontrollgränser bryter tillgängligheten. Objekt vars adress
lämnar den direkta åtkomsten utesluts. VLA/dynamiska ramar och setjmp-funktioner
optimeras ännu inte. Smala celler, allmänt olästa butiker och borttagning av
överflödiga adressproducenter återstår.

Verifierat lokalt: unity- och fil-för-fil-bygge samt 16 kombinationer i
`bench/vcc-wasm-cells.py` (wasm32/64 × O0–O3 × två källfiler), med både
kodformskrav och körning genom egna assemblern/länkaren och Node. Kraven
visar borttagna laddningar från oberoende structfält, två butiker till en,
tre laddningar till en efter asm-gräns, samt bevarade volatile/atomic-
operationer. Körfallen täcker också högerledseffekter, union/bitfält,
kontrollflöde och adresser som lämnar objektet. Kostnadsmätning och CI återstår;
detta är delprogress, inte full O2-täckning.

Kostnadsprov mot `opt/slot-values` (`e6bad4c`), båda värdbyggen med
`-O3 -march=x86-64-v3`, Callgrind hela processen inklusive start, 500
funktioner per prov, wasm32/64 och O1/O2:

| Prov | Ökning i värdinstruktioner |
|---|---:|
| Ren heltalskod, ingen ram | +0,185–0,186 % |
| Pekarladdningsslinga, ingen optimerbar ramcell | +0,298 % |
| Struct med två fält och upprepade åtkomster | +8,19 % |

Structprovet tar bort tre laddningar och en överskriven butik per funktion
(sex minnesoperationer till två). Mätningen visar adapterkostnaden när den
faktiskt arbetar; den uppfyller inte en generell kostnadsgräns på 1 % och
påstår ingen förbättring i programmets körtid. Binär-/källhashar, kommandon
och profiler finns lokalt i `work/opt-profile/wasm-cells-cost/result.json`.
Det gamla bygget avvisas av det nya positiva kodformskravet. Samma 16
kombinationer passerar också med ASan+UBSan. Kostnadsgranskning och CI är
fortfarande öppna leveransvillkor.

Separat liten kostnadsändring: högst 16 cellintervall sorteras med begränsad
insättningssortering i den gemensamma klassificeraren; större mängder behåller
`qsort`. Samma positiva 500-funktionsprov går från cirka +8,19 % till +7,03 %
hela processens värdinstruktioner mot basen. De tolv assemblerutdata för
kontroll-, sling- och structproven är byteidentiska före/efter sorteringsbytet.
De sanitiserade intervall-/värdekontrakten och 16 wasm-kombinationer passerar;
intervallprovet har också uttryckliga fall på båda sidor om gränsen 16.

Ett kort blandat C-prov efter sorteringsändringen kompilerar elva befintliga
regressionsfiler till wasm64 O2: djupa blandade uttryck, registerlokaler,
bitkastningar, ramceller, heltalsregler, djupa villkor, potenstvådivisioner,
atomics, minnesinbyggda och de nya ramcellsfallen. Resurshuvuden från LLVM 23
anges uttryckligen. Summan för elva separata processer är 96 456 389 till
97 034 041 värdinstruktioner (+0,599 %); enskilda filer ligger på +0,115 till
+1,337 %. `_BitInt`-runtimefilen avvisas redan av basbygget och räknas inte
som godkänd eller in i summan. Kommandon, käll-/binärhashar och feltext finns
lokalt i `work/opt-profile/wasm-cells-mixed-configured-cost/result.json`.
Detta avgränsade kompileringsprov ersätter inte den ordinarie fulla
prestandagrinden eller belägger programmets körtid; CI återstår.

### OPT5 integrationskontroll (2026-11-11)

På `opt/wasm-memory`, utifrån PR46 och wasm-steget: unity och fil-för-fil
bygger. 6120/6120 arm64-objekt/text är identiska mot värdekärnan e6bad4c
(2040 filer × O1-objekt/O2-objekt/O2-text), inklusive den nya portabla
wasm_frame_cells-regressionen. Sanitiserade cell-/värdekontrakt och alla
16 observable-kodkrav (fyra mål × O0–O3), med borttagen unread volatile
store som negativ mutation, passerar. CI kör nu även observable-kontraktet.

Hela egna wasm32-sviten: 149/149 pass, 30 körda XFAIL och 15
referensundantag. `parenthesized_function` var felaktigt märkt som XFAIL
även på wasm32; både f866a31-baslinjen och kandidaten passerar, så wasm32
kräver nu PASS medan wasm64:s kvarstående signaturbugg fortsatt körs XFAIL.
Hela egna wasm64-sviten: 153 pass, 31 körda XFAIL, nio referensundantag
och ett synligt oracle-compile-fel: lokal clang23 kraschar på isinf_sign:s
unsupported `_Float16`. Samma krasch reproduceras med f866a31-binären;
ingen referenskrasch döljs eller räknas som PASS. Loggar:
`work/opt-pr-tmp/wasm-memory-{own64,baseline-oracle,own32-final}.log`.
Detta är en lokal referensbegränsning, inte ett påstående att hela
wasm64-sviten är grön. Ny CI och bredare prestandagrind återstår.

### OPT6: kortslutna konstanta kontrollbeslut (delsteg)

Den målgemensamma hjälparen `emit/opt/control.c` används nu också i wasm:
rena kombinationer av `__builtin_constant_p`, `!`, `&&`/`||` och literal
`__builtin_strcmp` väljer bara den levande grenen vid O1–O3. Befintlig
constant_p-policy och O0-utdata bevaras; okända eller effektfulla uttryck
fortsätter genom den gamla vägen. `if_call_const` och `cond_dead_and`
passerar wasm32/64 och är obligatoriska. Nytt SDK-fritt prov
`bench/vcc-constant-control.py` kör alla fyra nivåer och ligger i CI för
båda wasm-lägena och native-målen. Inlining, konstant switch med borttagna
segment och WARN-statement expressions är fortsatt öppna; WARN-provet med
volatile asm `nop` behöver dessutom stöd för den instruktionen i wasm-asm.
Se OPT6-status i `PLAN-fas2.md` för bevis, kostnader och avgränsning.

### OPT5 fortsättning: privata stores genom strukturerade joins

På `1d8e29a` (flödesförälder `7691f6c`) använder den separata wasm-adaptern
`arch/wasm/emit/flow.c` den gemensamma `emit/flow`-grafen och budgeterade
masklösaren. Typade kontrollhändelser fångas från funktionsinträdet under
ordinarie emission; unika öppningstoken skiljer branchmål även när lexikalt
djup återanvänds. Ingen ny AST-vandring, SSA eller assemblertextparsning.

Privata exakta 4/8-byte heltals-/flyttalsceller får överskrivnings-DSE över
if/else-joins och nästlade diamanter. Rena kontrollfences bryter fortfarande
lokal forwarding; riktiga minnes-/anropsbarriärer bryter också DSE och får
bara sammanslås inom samma kontrollepok. Det bevarar barriären efter en tom
gren på alla vägar. Befintlig renderer behåller adress-/RHS-effekter och
lokala värdefångster. Full konvergens och separat staging krävs före ändring.
Alla celler är konservativt levande vid utgång: generell unread-DSE ingår
inte. Loopar, switch/dispatchers, goto, asm, SJLJ och dynamiska ramar avstår.
Capture-, cell- och arbetsgränser ger fallback; O0/noopt avstår. Grafen
byggs först efter komplett emission, exakt cellklassificering och verkliga
upprepade privata stores genom en join.

114 konfigurationer passerar genom egna verktyg och Node: 10 nya flödesfall
(wasm32/64 × O0–O3 kodform/körning, samt O2/noopt-kodform), 16 befintliga
cellfall och 88 opt-regelkontroller. Positiva fall har exakt en slutstore;
volatile/atomic, läsningar före overwrite, utgångar, alias/escape, effekter,
flyttalsbitar och uteslutna kontrollformer kontrolleras. En isolerad mutant
utan fence-epokvillkor avvisas av den avsedda tom-else-kodkontrollen på
båda bredderna; originalkällorna lämnas intakta. Unitybygge med `-j 1`,
fristående syntaxkontroll av ändrade wasm-C-filer och oberoende review är
klara. Snabbgrinden kör nu `bench/vcc-wasm-flow.py`; fjärr-CI återstår.

Hela egna SDK-baserade sviter på samma revision avslutar med status 0:

| Mål | PASS | Körda XFAIL | Skips |
|---|---:|---:|---|
| wasm32 | 178/178 | 36 | 16: 12 clang-reject, 1 noref, 3 ref-fail |
| wasm64 | 182/182 | 37 | 11: 8 clang-reject, 1 noref, 2 ref-fail |

Undantag räknas inte som PASS. Loggarna är
`work/opt-pr-tmp/wasm-flow-full32.log` och `wasm-flow-full64.log`.
Fokuserad validering, källbevis och isolerade mutantloggar/hashar finns i
`build/linux/flow-evidence/`. Efter integration av exakt flödesförälder
`f7327bd` till `1458a90` passerar samma 114 konfigurationer och fristående
syntaxkontroll igen. 66 O0-/O2-noopt-par är byteidentiska mot föräldern;
två ytterligare par avvisas av bådas befintliga ILP32-layoutassert i
`atomic_ops`. SDK-helsvitssiffrorna ovan gäller fortfarande `1d8e29a`.

Matchade clang 23-värdbyggen med `-O3 -march=x86-64-v3`, exakt `f7327bd`
mot `1458a90`, ger följande separata Callgrind-underlag. Hela processen
inklusive start räknas, wasm32/64 O2 `-S`, seriellt en gång per prov/binär:
200-funktionskontroller med rak kod, villkor och uteslutna loopar kostar
+0,314–0,552 % värd-Ir med byteidentisk assembler och objekt. Två positiva
diamantprov kostar +4,494–4,683 % extra värd-Ir; stores går från 800/1000
till 200 och vanliga `-c`-objekt minskar 2,886–3,325 %. Aktiv analys är
alltså dyrare än 1 % i dessa koncentrerade positiva prov.

En fast blandning av befintliga regressionsfiler ger sammanlagt +0,106 %
värd-Ir på wasm32 (10 filer) och +0,095 % på wasm64 (11 filer).
`atomic_ops` utesluts på wasm32 eftersom båda byggen avvisar dess
LP64-layoutassert; detta är inte PASS. Två assemblerutdata ändras per mål.
Kommandon, hashmanifest, råprofiler, objekt och undantag finns i
`build/linux/flow-evidence/cost/result.json`; hela tabellen står i
`PLAN-fas2.md`. Relocerbar objektstorlek är inte laddad kodstorlek eller
programfart, och detta ersätter inte full prestandagrind eller fjärr-CI.
Programkörtidsvinst är fortfarande omätt; bredare kontrollflöde, generell
DSE, OPT5/O2 och G5/G7 återstår.

### Begränsad skalär inlining (2026-11-11)

Statiska funktioner med en ren skalär retur kan nu instansieras i wasm med
bindningar i typade lokaler. Argument värderas en gång; en budget per
anropare och konservativ fallback begränsar arbete och kodtillväxt. Det är
inte stöd för generella funktionskroppar, minnesåtkomst eller rekursiv
inlining. Fyrmålsprov, wasm-sviter och kostnad finns i PLAN-fas2.md.
