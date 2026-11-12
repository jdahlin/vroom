# VIR (Vroom IR): SSA-nivån ovanpå vandraren

**Mål:** en liten, snabb IR i SSA-form för det som inte går utan SSA: riktig
registerallokering, global värdenumrering och minnesvidarebefordran över block.
Den ska användas av x86-64 och arm64 (wasm senare), kosta ingenting på `-O0`
och inte upprepa LLVM IR:s kända misstag.  Formatet och verktygen är specificerade i
`SPEC-vir.md`; underlaget finns i
`RESEARCH-vir.md` (LLVM IR:s problem med källor, och vad B3, Cranelift, Zig,
Go, Turboshaft, LuaJIT, QBE och TPDE valde i stället).  Registerallokeringen
har ett eget underlag, `RESEARCH-regalloc.md`.

**Beslut (2026-11-11, frågerunda med Johan):**
- `-O0` behåller dagens direkta kodgenerering.
- Semantiken är definierad som standard (omslag, ingen typbaserad alias,
  null är en vanlig adress), med striktare regler som flaggor man slår på.
- x86-64 och arm64 är båda mål.  (Första frågerundan sa "arm64 först"; den
  byggde på en gren som låg 566 commits efter `main` och saknade x86-64.)
- VIR ligger **bara på `-O2`**.  `-O1` förblir OPT0–OPT8 utan SSA.

## Utgångsläge (2026-11-11, `main` 8bdc940-serien + x86)

- Tre kodgeneratorer väljs i `emit/tu.c`: arm64 (`arch/a64/emit/`, egen
  AST-generator), x86-64 (den delade vandraren `emit/walk/` + sänkningen
  `arch/x64/emit/` genom `LO()`), wasm (`arch/wasm/emit/`, egen).
- `PLAN-fas2.md`, **Gemensam optimerare utan SSA** (OPT0–OPT8), äger de
  optimeringar som går utan IR: algebraregler i `emit/opt/`, postpass i
  `emit/rec/`, blockgraf och liveness i `emit/flow/`.  TODO.md säger
  uttryckligen "No new IR/SSA".
- `PLAN-x86.md`, "Vad som inte går utan SSA", pekar ut vägen om mer behövs:
  ett valfritt SSA-pass med virtuella register, bara för `-O2`.  **VIR är det
  passet.**  Den här planen ändrar alltså inte OPT0–OPT8 utan lägger en nivå
  ovanför; TODO-raden behöver preciseras när V1 börjar.
- Värden i vandraren är fysiska register som målet lämnar ut (`LO(treg)`), med
  spill runt anrop och omstart av funktionen när temporärerna tar slut.  Det
  finns ingen registerallokerare på något mål.
- Mätt på arm64 (PLAN-fas2 status 2.6, 80 kärn-TU:er, statiskt): vcc 1,51×
  clang -O2; clangs bakände + mem2reg/SROA ensam 1,15×.  Skillnaden är i
  huvudsak värden i register över hela funktionen.

## Design

### Var VIR sitter

```
cc/:s AST ──▶ vandraren (emit/walk/) ──LO()──▶ x64_* / a64_*  ──▶ poster (emit/rec/) ──▶ kod
                     │                              ▲
                     └──LO()──▶ vir_*  ──▶ VIR ──▶ pass ──▶ registerallokering ─┘
```

- **VIR är ett virtuellt mål för `LO()`.**  Vandraren kompileras en gång till
  med `LO(name)` → `vir_##name` (mallen PLAN-x86 redan förbereder för en andra
  CPU).  Alla beslut på C-nivå (vikning, nåbarhet, inlining,
  `__builtin_constant_p`, switch-strategi, cleanups, `emit/opt/`-reglerna) görs
  då av samma kod som i dag.  Det blir ingen fjärde kopia av C-reglerna.
- `vir_treg` lämnar ut ett nytt variabelnummer och tar aldrig slut;
  `vir_tfree` gör ingenting.  Varje `LO`-operation läser sina variabler som
  SSA-värden och skriver en ny definition (Braun m.fl. 2013: SSA byggs medan
  operationerna kommer, etiketter med bakåthopp förseglas när funktionen är
  klar).  Locals utan `lnoreg` blir variabler via `LO(local)`; övriga är
  stackplatser som i dag.
- En VIR-funktion byggs **för ett bestämt mål**: `LO(classify)`, `ldmem`,
  `i128mem` och datamodellen besvaras av den riktiga CPU:n.  VIR är alltså inte
  måloberoende, lika lite som LLVM IR efter clang.
- **Sänkning:** efter passen och registerallokeringen anropas den riktiga
  CPU:ns `LO`-operationer med de tilldelade registren.  Därefter gäller dagens
  väg oförändrad: `emit/rec/`-passen, `twoaddr.c`, ram och prolog, skrivare och
  kodare.
- **Reträtt per funktion:** kan VIR inte bygga en funktion genereras den med
  den vanliga vägen (samma vandrare, riktiga `LO`).  I 1.0 gäller det:
  `returns_twice`-anrop, VLA/`alloca`, `__int128`/`_BitInt` i registerpar
  (`w*`/`big*`), x87 `long double`, vektortyper.  Täckningen mäts per steg.

### Representation

- Per funktion och tråd, i `ecx`; inget muterbart delas mellan funktioner.
- **Instruktion, 16 byte:** `op:u8, ty:u8, fl:u16, a:u32, b:u32, c:u32`.  Ett
  värde är indexet för instruktionen som definierar det.  Extra operander
  (anropsargument, blockargument, switch-fall, asm-operander) ligger i
  `u32 extra[]`.
- Konstanter är instruktioner (`iconst`, `fconst`, `symaddr sym+off`,
  `slotaddr`, `labeladdr`), unika per funktion och utan plats i något block.
- Block är sammanhängande intervall i omvänd postordning, med
  blockparametrar (inga phi-noder), loopdjup och flaggor.  Grafen byggs med
  `emit/flow/graph.c`.
- Inga användarlistor.  Ett pass ändrar på plats (`nop`, aliastabell) eller
  skriver en ny array i ett svep framåt.
- Sidotabeller per värde: källposition (`LO(dbgrow)`), en arbetscell för
  passet som körs.

### Typer

`i32`, `i64`, `f32`, `f64`, `ptr`.  `i8`/`i16` finns bara som bredd på
laddningar och lagringar.  Jämförelser ger `i32` 0 eller 1.  Aggregat är
aldrig värden: de bor i stackplatser och flyttas med `memcpy`.

### Semantik

- Heltal slår om.  Skiftantal tas modulo bredden.  `sdiv` av minsta talet med
  −1 slår om.
- Division med noll och åtkomst genom en ogiltig pekare är **omedelbart
  odefinierat**: operationen får tas bort om resultatet är oanvänt, men inte
  flyttas dit den inte kördes förut.
- Ingen `undef`, ingen `poison`.  Varje värde är ett konkret bitmönster och
  minnet är konkreta byte.  En variabel som läses före första skrivning är 0.
- **Flaggor man slår på** (`NSW`, `NUW`, `EXACT`; senare `INB` och typtagg på
  laddningar och lagringar): brott mot flaggan är omedelbart odefinierat vid
  den instruktionen.  Ett pass som flyttar instruktionen nollar flaggan.
  Byggaren sätter `NSW` när TU:n saknar `-fwrapv`/`-fno-strict-overflow`;
  inget 1.0-pass beror på den.
- **Pekare:** `ptr` är en egen typ.  `p2i` exponerar objektet, `i2p` kan peka
  på vilket exponerat objekt som helst, tur och retur viks aldrig.  En pekare
  ersätts bara av samma SSA-värde.  En stackplats vars adress bara används som
  adressoperand och i `ptradd` är oflydd.
- Laddningar och lagringar bär bredd, utvidgning, justering och `volatile`.
  Volatila åtkomster tas aldrig bort, slås ihop eller byter ordning.
- Effekter är en egenskap hos opkoden (ren, läser minne, skriver minne,
  återvänder kanske inte), förfinad på anrop av `const`/`pure`/`noreturn`.
- Flyttal: närmaste avrundning och osynliga undantag, om inte funktionen har
  `fenv`; då är flyttalsoperationer effekter.  Vikningen ger aldrig ett NaN.
  `nofp` (`-mgeneral-regs-only`, kärnans `-mno-sse`) förbjuder flyttalsvärden.

### Operationer

De följer `LO()`-gränssnittet, så att byggaren är en tunn översättning:

| Grupp | Operationer |
|---|---|
| Konstanter | `iconst fconst symaddr slotaddr labeladdr tlsaddr` |
| Heltal | `add sub mul sdiv udiv srem urem and or xor shl lshr ashr neg not umulhi smulhi` och bitoperationerna (`clz ctz popcnt bswap`) |
| Bredd | `sext zext trunc` |
| Jämförelse, val | `icmp fcmp select` |
| Flyttal | `fadd fsub fmul fdiv fneg fsqrt cvt fbits` |
| Pekare | `ptradd psub p2i i2p` |
| Minne | `load store memcpy memset` |
| Atomära | `atomic` (WK_ALOAD … WK_AFENCE) |
| Anrop | `call` (med `wkarg[]` från målets `classify`), `param`, `proj`, `va_start`, `va_arg` |
| GNU C | `asm` (begränsningssträngen behålls till allokeringen), `frame`, `barrier` |
| Avslut | `br brif switch ibr asmgoto ret trap` |

- `ibr` (beräknad goto) listar funktionens alla adresstagna etiketter.  De
  blocken har inga parametrar, så variabler som lever där hålls i stackplatser.
- `asmgoto`-kanter är vanliga kanter som får delas; utdata är resultat av
  avslutet.
- `trap` är alltid en instruktion (`ud2`/`brk`): kod faller aldrig av en
  funktions slut (objtool).

### Pass på `-O2`

1. **Bygg** (vandraren genom `vir_*`): SSA under byggandet, vikning och lokal
   CSE vid tillägg.
2. **Förenkla:** triviala blockparametrar bort, konstanter framåt, vikta
   grenar, onåbara block.
3. **Redundans:** värdenumrering över dominatorträdet, lagring→laddning och
   döda lagringar på oflydda stackplatser, död kod.
4. **Allokera:** girig tilldelning i blockordning på SSA-formen, med utkastning
   av värdet vars nästa användning ligger längst bort (SPEC-vir.md 15.5–15.11;
   heltals- och flyttalsklass, parallella flyttar per kant, fasta register för
   `div`, skift och argument, tips i stället för sammanslagningsfas).  Inget
   LICM i 1.0, med ett undantag (beslut 2026-11-12): loopkonstanter i raka
   innerloopar utan anrop lyfts ut (`vv_pin`, `emit/vir/vec.inc`).
5. **Sänk** genom den riktiga CPU:ns `LO`.

Passlistan är fast: vanliga funktionsanrop, ingen passhanterare, inga
fixpunkter utan budget (samma regel som `emit/flow/`).

### Textform, kontroll och tolk

- Skrivare och parser för en textform; `vpp -fvir-dump=STEG`.
- Verifierare efter varje pass i test- och sanitizerbyggen.
- `virtest` kör namngivna pass på `.vir`-filer; ögonblicksbilder i
  `ccs/vcc/tests/vir/`.
- `virrun`, en tolk, är referenssemantiken.  Varje pass kontrolleras genom
  tolkning före och efter, på testerna och på slumpade funktioner
  (`ccs/fuzz/fuzz-vir`).

### Filer

- `ccs/vcc/vir/`: `vir.h`, `virimpl.h`, `print.c`, `parse.c`, `verify.c`,
  `interp.c`, `simplify.c`, `gvn.c`, `mem.c`, `regalloc.c`.  Egen modul
  `vir_SRC`.
- `ccs/vcc/emit/vir/`: `build.c` (`vir_*`-implementationen av `LO`) och
  `lower.c` (VIR → riktiga `LO`), i `emit`-unity-TU:n.
- Namn: `vir_*`, `VO_*`, `VT_*` (`V_CONST`/`V_REG` är upptagna).

## Steg

| # | Leverans | Kriterium |
|---|----------|-----------|
| V0 | **Underlag och plan.** `RESEARCH-vir.md`, `PLAN-vir.md` | Granskade av Johan |
| V1 | **Format och verktyg.** `vir.h`, skrivare, parser, verifierare, tolk, `virtest`, `fuzz-vir` | Skriv–läs–skriv identiskt på alla tester; verifieraren avvisar en mutationsmängd; standardbygget byte-identiskt (`vbench.py check`) |
| V2 | **Byggare och enklaste sänkning på x86-64.** Vandraren mot `vir_*`; sänkning där varje värde bor i en stackplats; `VCC_VIR=1`; reträtt per funktion | Med `VCC_VIR=1`: `bench/vcc-x86.py`, `vcc-abi.py --target x86_64`, gcc-torture samma godkända som utan; täckning rapporterad per korpus; utan flaggan byte-identiskt |
| V3 | **Täckning.** Bit-fält, poster i anrop, varargs, atomära, flyttal, switch (vandrarens sänkning), beräknad goto | Täckning ≥ 95 % av PostgreSQL:s funktioner och av vcc:s egna; x86-kärnan bootar med `VCC_VIR=1` (asm-funktioner går den vanliga vägen); objtool utan varningar |
| V4 | **Registerallokering.** Nästa-användning-kedjor bakåt, girig tilldelning framåt, fasta register, parallella flyttar per kant, `vir_alloc_check` | Instruktioner på en fast TU-mängd under dagens `-O2`; allokerarens tid och räknare (spill, omladdningar, flyttar) redovisade för sig; kontrollen grön på testerna och `fuzz-vir` |
| V5 | **Pass.** Förenkling, värdenumrering, minnesvidarebefordran, död kod | Per pass: tolkekvivalens, instruktioner och `vbench.py`-tid före och efter, i planen |
| V5b | **Inline asm.** `x64_asm` delas i planering och emission (SPEC X6); `asm` och `asmgoto` byggs | Utan flaggan: kärnobjekten och `vas-corpus.py` byte-identiska; med: täckning ≥ 99 % av x86-kärnans funktioner |
| V6 | **arm64.** Kräver att arm64 har en `LO`-sänkning (TODO.md "arm64 and wasm on the shared walker"); VIR driver flytten eller väntar på den | ctests 1729/1729, `vcc-abi.py`, arm64-kärnan bootar med `VCC_VIR=1`; `vcc-obj.py` 840/840 |
| V7 | **VIR är standard på `-O2`.** | Förslag: ≤ 1,25× clang -O2 i statiska instruktioner; steg 2-självbygge ≤ 1,5× långsammare än clang-bygget (2,6× i dag); `-O2`-kompilering ≤ +15 % totalt; `-O0`/`-O1` byte-identiska hela vägen |
| senare | `NSW` och typtaggar används; SROA av oflydda poster; `returns_twice` med onormala kanter; VLA; `__int128`; wasm | — |

"VIR 1.0" är formatet och semantiken så som de ser ut när V7 passerar.

V1 generator: `bench/virgen.py --seed=N --size=N` ger deterministiska
heltalsprogram med initialiserade stackslotar och förgreningar. 101 fall,
inklusive maxstorlek, verifieras och skrivs–läses–skrivs under ASan-CI.
Detta är en första del: flyttal, anrop, fler kontrollflöden, `virrun` och
ekvivalenstestning återstår. Genererade program använder inga externa anrop.

## Validering

- Fram till V7 ligger VIR bakom `VCC_VIR=1`; varje commit lämnar
  standardutdata byte-identiskt (`vcc-same.py`, `vbench.py check`).
- Två vägar genom samma vandrare är ett test i sig: samma program byggt med
  och utan `VCC_VIR=1` ska bete sig lika i C-sviterna, ABI-testerna och
  kärnbooten.
- Tolken jämför före och efter varje pass; `fuzz-vir` med ASan/UBSan.
- Fartgrind: `bench/vbench.py gate` med flaggan av (±0,1 %) och på (redovisad).

## Risker och öppna frågor

- **G5 mäter `-O1`.** VIR är beslutat till `-O2`, så G5 (`-O1` inom X % av
  clang -O1) måste nås av OPT0–OPT8 ensamt.  VIR:s egen grind är V7.
- **Ändringar i vandraren och x86-sänkningen** är genomgångna mot koden i
  SPEC-vir.md avsnitt 19 (X1–X9).  Störst är X6 (inline asm: `x64_asm` delas
  i två) och X1 (vandraren instansieras två gånger); anrop med värden i
  register (X4) och önskat resultatregister (X7) behövs först vid V4.
- **`LO` är inte rent.**  `wkarg` och delar av `walk.h` är SysV-specifika.
  VIR ärver det tills arm64 flyttar; V6 beror på den flytten.
- **Var x86-64 testas.**  `bench/vcc-x86.py` körs på en x86-64-värd.  V2–V5
  behöver den maskinen (eller att V6 tas före, vilket kräver arm64 på `LO`).
- **Omedelbart odefinierat i stället för poison** gör flaggade instruktioner
  omöjliga att spekulera utan att flaggan nollas.  Medveten gräns.
- **Två byggen av vandraren** (riktig CPU och `vir_*`) ökar kompilatorns
  storlek och byggtid; mäts i V2.
- **Regeln "No new IR/SSA" i TODO.md** och rubriken "utan SSA" i PLAN-fas2
  gäller OPT0–OPT8.  De preciseras ("på `-O0`/`-O1`") när V1 börjar.
- **Öppet:** benchmarksvit och X för G5 (PLAN-release).

## Status

### Vektorer och loopvektorisering (2026-11-12): första skivan, TSVC_2 151/151

Underlag och design: `RESEARCH-vectors.md` (ändrar SPEC 1.3/1.4: VIR har egna
vektorvärden, och en looptransformation).  Gjort, i den ordningen:

- **Byggaren tog inte flyttalsargument.**  Ett `f32` i argumentplatsens
  8-byteord lagrades som `f64` (`vw_store`), så varje funktion som anropade
  något med en `float`-parameter föll tillbaka.  `tsvc.c`: 7 av 158 funktioner
  byggda före, 158 efter.
- **`bench/vcc-tsvc.py`**: TSVC_2 (`fetch-testsuites.sh TSVC_2`), 151 loopars
  kontrollsummor mot clang -O3, direkt emitter och VIR; `--why` ger
  vektoriserarens skäl per loop.
- **Typer och operationer** (SPEC 4, 7.5): `f32x4`, `i32x4`; de skalära
  operationerna tar dem genom `OK_T`; `vsplat vseries vicmp vfcmp vbitsel vs2f
  vrev vreduce`.  Mask = `i32x4` med 0/-1.  Vektorer i flyttalsklassen,
  spillplats 16 byte (`4 << klass`).
- **arm64-sänkning** (`emit/vir/a64.c`, `vl_vec`): NEON som kodade ord, mot
  clangs assembler; registerflytt och byte är `mov`/`eor v.16b`.  **En vektor
  i en spillplats sänks inte** (funktionen tar då den vanliga vägen): saknar
  en post för `ldr/str q` mot ramen.
- **`nsw`** på tecknad `+ - *` från vandraren; `-fwrapv`/`-fno-strict-overflow`
  (`cc_opts.wrapv`) stänger av det.  Bara vektoriseraren läser flaggan.
- **Vektoriseraren** (`emit/vir/vec.inc`, i pre-IR:en efter if-konvertering):
  innersta loopar med test i huvudet och rak kropp; induktioner och
  `i32`-summor; laddningar/lagringar `symbol ± 4·t`; beroenderegeln i
  RESEARCH 6.3; skalärloopen blir kvar som rest.  Av under
  `-mgeneral-regs-only` och med `VCC_VIR_VEC=0`; `VCC_VIR_VEC=r` rapporterar.

Mätt (M1 Pro, `iterations` 256): direkt emitter 7,2 s, VIR 4,3 s (5,1 s före
vektoriseraren), clang -O3 1,5 s; 151/151 kontrollsummor på alla tre.
**28 av 120 innerloopar vektoriserade** (clang: 77 loopar i 66 funktioner).
De vektoriserade looparna går 5–10× fortare än den direkta emittern och
1,3–5× långsammare än clangs (ingen interleaving, adresserna räknas om i
loopen).  Skälen för resten: flyttalsreduktion eller rekurrens 17, lagring
med annat steg än elementet 16, anrop 11, adress som inte är symbol + linjär
(pekare) 10, lagrat värde läst 1–3 varv senare 10, värde som inte kan bli
banor (gather, `double`) 9, fel ordning inom kroppen 6.

Validering: `tests/vcc/vir_vec_loops.c` (47 loopar × varvtal 0–9 och 37 mot
clang: rest 0–3, nedåtlöpande, avstånd −5..5 inom en array, lagring före
laddning, summor, val, invariant ur minnet); `vcc-ctests.py` med `VCC_VIR=bl`
2079/2082 (de tre `.Lswitch.values`-felen fanns före), utan VIR oförändrat;
ASan/UBSan rent på TSVC och testet; `vir-test.py` 124/124.

**Andra omgången (samma dag): 43 av 120 loopar, VIR 2,6 s.**

- **Körtidskontroll av överlapp.**  Två åtkomster som varken är ett objekt
  eller två symboler (pekare mot vad som helst, invariant laddning mot en
  lagring, en array på okänt avstånd `a[i]`/`a[i+k]`) jämförs som byteintervall
  i VPH; möts två går loopen skalärt.  Högst 8 par (som LLVM).
- **Pekare** som bas: parametrar, och adresser räknade som heltal (`i2p` av
  laddad global pekare + index).
- **Pekarinduktioner** i vektorkroppen i stället för omräknat index.
- **Induktioner**: flera steg per varv (`j++` två gånger), steg som är ett
  värde (versioneras på steg = 1), teckenlös `i32`-räknare med steg 1 under
  teckenlös gräns, `size_t`.
- **Vektorspill sänks** (`ldr/str q` genom x16, plats till plats genom v31):
  ingen återgång till den direkta emittern längre.
- **d8–d15 för skalära flyttal över anrop** (`emit/vir/a64.c`): erbjuds
  allokatorn som anropsbevarade, sparas vid ingången och återställs före
  varje retur och syskonanrop.  Inte när en vektor lever över ett anrop
  (`vl_vcross`; ingen gör det i dag) och inte med
  `-fasynchronous-unwind-tables` (CFI för platserna saknas).  En
  flyttalsackumulator i en loop inuti en loop med anrop gick genom minnet
  varje varv: s311, vsumr, vdotr 0,45 s → 0,08 s (clangs tid).

Mätt: direkt emitter 7,5 s, VIR 2,65 s, clang -O3 1,49 s; 151/151.  Skäl för
de 77 looparna som är kvar: steg ≠ element 17, flyttalsreduktion/rekurrens 17,
värde som inte kan bli banor 10, lagrat värde läst 1–3 varv senare 10, adress
inte bas + linjär 6, anrop 6, ordning i kroppen 6.  Av de 66 funktioner clang
vektoriserar tar vi 36; störst skillnad: 9 med villkorlig lagring (clang
skalariserar lagringen per bana), 7 flyttalsreduktioner (clang: ordnade), 6
med steg som bara är känt i körtid i en multiplikation.

Tester: `tests/vcc/vir_vec_loops.c` (nu även överlappande pekare, 10
spillda vektorer, steg 1 och 3, okänt avstånd, teckenlösa räknare),
`tests/vcc/vir_fp_callee.c`.  `vcc-ctests.py` med `VCC_VIR=bl` 2080/2083 (samma
tre), utan VIR 2083/2083; ASan/UBSan rent.

**Tredje omgången (samma dag): skalärloopar.  VIR 6,3 G cykler, clang 4,75.**

- Flyttal laddas och lagras direkt i v-register (`ldr/str s/d`) när adressen
  är register + offset eller register + index; förut via x16 och `fmov`.
- `s2f`/`u2f` av heltalskonstant och `fext`/`ftrunc` av flyttalskonstant viks.
- **Loopkonstanter lyfts ut** (`vv_pin` i `vec.inc`): symboladresser,
  flyttalskonstanter ≠ 0 och jämförelsegränser som inte är en immediate görs
  en gång framför de raka innerloopar vektoriseraren ändå tittar på (högst 6,
  inga loopar med anrop).  **Det här är ett avsteg från "Inget LICM i 1.0"**
  ovan, begränsat till konstanter; `VCC_VIR_VEC=n` stänger av det.  Effekt på
  TSVC: −19 % instruktioner, −7 % cykler.  **Beslut (Johan, 2026-11-12):
  behålls**, som undantag i passlistan ovan; bredare LICM är fortfarande
  utanför 1.0.
- `bench/vcc-tsvc.py` skriver cykler (`/usr/bin/time -l`): maskinen är delad
  och sekunderna varierar.

Mätt i cykler (G): direkt emitter 23,6, VIR 6,33, clang -O3 4,75; 151/151.

**Fjärde omgången (samma dag): 8-bytesbanor och val.**

- **`f64x2` och `i64x2`** (SPEC 4, 7.5; operandslagen `OK_VI`/`OK_VF`/`OK_VRED`
  följer resultatets banbredd): typer, tabell, verifierare, NEON (`.2d` är
  `.4s` med storleksbiten), och vektoriseraren är generisk över banbredd
  (bredden är loopens första lagring; VF 2 eller 4).  Ingen 64-bitars
  multiplikation; en loop som blandar 4- och 8-bytesdata går skalärt.
- **Val (`select`)**: jämförelser → `vicmp`/`vfcmp` → `vbitsel` var aldrig
  testade förut, eftersom testens val hade laddningar i armarna och inte
  if-konverterades.  Nu 16 jämförelser × två banbredder mot clang.  Två hål
  det visade: (1) **byggaren tog inte `float`-villkorsuttryck** (`c ? x : y`
  med `float`: temporärens 8-byteord lästes som `f64`; `vwslot.f32w`), så
  sådana funktioner gick inte genom VIR alls; (2) heltalsval kommer som
  `trunc(select i64(ext, ext))` från vandraren och känns nu igen (`VK_W32`).
- Looprotation i sänkningen (testet kopierat till slutet av loopen) provades:
  −1,3 % cykler på TSVC, 0 på sqlite, +0,9 % kod.  Inte infört (samma slutsats
  som tidigare i `vl_dupok`).

`tests/vcc/vir_vec_wide.c` (51 loopar på 8-bytesbanor × varvtal 0–9 och 37).
TSVC oförändrat (allt där är `float`): 43 av 120, 6,3 G cykler.  SQLites
speedtest1 genom VIR: samma verifieringshash som clang, 2,29 G cykler (clang
2,01, direkt emitter 3,05); 5 av 565 innerloopar vektoriseras där (resten är
listvandringar, anrop och byteloopar).

Kvar (RESEARCH 6.6 och 5): `i8x16`/`i16x8` (byteloopar: kräver att
i32-räkningen får smalnas); `double` ur `int` (`(double)i`); invarianta
uttryck som inte är konstanter; CFI för d8–d15; steg ≠
elementstorlek;
interleaving; ordnade flyttalsreduktioner; övriga fyra bantyper; x86-64 och
wasm; tolken och `virgen` känner inte vektorer; kärnan, PostgreSQL och SQLite
är inte körda med VIR efter detta.

### V0 (2026-11-11): klar för granskning

- `SPEC-vir.md` (utkast 0.1): hela formatet (16-byte-instruktion, block,
  stackplatser, `extra`), typer, minnesmodell, alla 1.0-operationer med
  effekter, ABI-signaturer, textform med grammatik, verifierarens 27 regler,
  tolken, passens kontrakt, registerallokeringens gränssnitt, verktyg, tester
  och gränser.  Den listar sju ändringar som krävs utanför `vir/` (X1–X7) och
  sju öppna frågor (O1–O7).  Planens avsnitt Design ovan är en sammanfattning;
  där de skiljer sig gäller specifikationen (t.ex. bara `br` bär
  blockargument, och `__builtin_constant_p` avgörs av vandraren).

- `RESEARCH-vir.md`: 29 problem hos LLVM IR i tre grupper (semantik, struktur
  och kompileringstid, C-gränssnittet) med källor, och regeln VIR tar av
  varje; tabell över andra kompilatorers val.
- Den här planen.  Första utkastet (godkänt i planläge) utgick från en gren
  utan x86-64 och hade en egen AST-byggare och arm64 som första mål.  Efter
  läsning av `main` är byggaren i stället ett virtuellt `LO`-mål, x86-64 är
  första mål, och VIR ligger på `-O2`.
- Ingen kod ändrad.

### Förberedelse (2026-11-11): X1–X3 på `main`, utan ändrad kod

Dokumenten ligger på `main`, och tre utdataneutrala ändringar i vandraren är
gjorda (SPEC-vir.md avsnitt 19, X1–X3); inget under `ccs/vcc/vir/`.

- **X2:** `LO(slot)(e, off, size, align, kind)` anropas en gång per utlämnad
  frameoffset (`wk_pslot`, båda återanvändningsvägarna i `wk_slot`,
  `wk_local`; `kind` är `WK_SLOT_TEMP/LOCAL/PARAM/FUNC`).  `x64_slot` är tom.
  Den statiska hjälpfunktionen `x64_slot` i `fp.c` bytte namn till
  `x64_tmpslot` (namnkrock som specen inte såg).
- **X3:** `L_VAR` i `lval`-uppräkningen; `x64_load`/`x64_store`/`x64_laddr`
  ger internt fel.  Ingen producerar den än.
- **X1:** `bench/gen-walkprefix.py [--check]` genererar
  `emit/walk/prefix.h` (130 namn); `WK_PREFIX`/`LO_PREFIX` ger en andra
  instans.  Bevisat med ett engångsbygge (inte incheckat): `walk/*.c` med och
  utan `-DWK_PREFIX=vw_ -DLO_PREFIX=vw_` ger 46 globala symboler vardera utan
  överlapp, inga oprefixade `wk_`/`x64_`-referenser kvar, 74 `vw_`-operationer
  som ett virtuellt mål ska ge.  Ingen andra instans är tillagd; den måste bli
  en egen TU (inte `emit`-unity).
- **Verifiering** (bas = commit före kodändringarna, `vbench.py mkbase`):
  x86-64 `-S` O0/O1/O2 på 253 testfiler (759 kompileringar) identiska;
  `vcc-same.py` 14687/14693 (de 6 avvikelserna är `-E`-radmarkeringar med
  basbygget egen include-sökväg, övriga lägen identiska); `vbench.py check`
  171/171; `vcc-ctests.py --suite vcc` 252/252 (1 xfail); `vcc-sema.py batch`
  878/879 (modpost.c, som väntat); `vbench.py gate --base` ok (högst +0,09 %
  instruktioner).  Båda byggena (unity och `VCC_UNITY=`) utan nya varningar.

### V1, första halvan (2026-11-11): format, textform och verifierare

V1 parser: en tom blockkropp ger nu verifieringsfelet S3 utan att först
anropa `memcpy` med en nullkälla och längd noll. Regressionsprov och hela
VIR-sviten passerar under ASan/UBSan (119/119); mutationsprov med seed 42
passerar 1 000/1 000. Övriga V1-grindar och VIR-stegen återstår.

Klart: `ccs/vcc/vir/` (modul `vir_SRC`, egen unity-TU, länkad bara in i
`virtest`; inget i `vpp`, `vccd` eller `emit/` ändrat).  `vir.h` (format,
opkodstabell som skrivare, parser och verifierare delar), konstruktion med
hash-consade konstanter, `vir_cfg`, `vir_dom` (Cooper–Harvey–Kennedy, pre/post
per block), `vir_uses`, `vir_canon`, `print.c`, `parse.c`, `verify.c` (alla
27 regler i SPEC avsnitt 12), verktyget `virtest` och `bench/vir-test.py`.
SPEC rättad till utkast 0.4 i egen commit (lista i PR:en).

- **Tester:** 118/118 (`ccs/vcc/tests/vir/`): 9 operationsfiler där varje
  instruktion och flagga skrivs–läses–skrivs identiskt, 56 verifierarfall (minst
  ett per regel, exakt den regelns id), 39 syntaxfel, 7 canon-fall, 6
  dominatorfall (tre irreducibla grafer) och `--selftest` för byggar-API:t.
- **ASan+UBSan** (`make vcc-asan`): 118/118, och 9000 slumpade mutanter av
  testenheterna (`--mutate`) utan krasch.
- **Standardutdata oförändrad:** `vbench.py check --base` (bas = `origin/main`)
  171/171 identiska; `vbench.py gate --base` ok (högst +0,07 %).  Båda byggena
  (unity och `VCC_UNITY=`) utan nya varningar.
- **Återstår av V1:** tolken `virrun`, `virgen`, `fuzz-vir`, `--equiv` och
  mikrobenchmarkarna i SPEC 22.8.  Inga pass finns, så `PASSES:` godtar bara
  `canon`.

### V4, första delen (2026-11-11): allokeraren och kontrollen på ett syntetiskt mål

Gren `claude/vir-regalloc-impl-f22a1d` på `main` (V1:s första halva);
ingen kod i V1 ändrad.
- **Gjort:** SPEC 15.1–15.2 som gränssnitt (`vir/alloc.h`: `vir_regs`,
  `vir_fix`, `vir_alloc_out` i trådens reservationer), loopskogen som
  intervall av blockföljden (`loops.c`, `vir_loops_build`, ersätts av
  `vir_canon` i V2), `vir_alloc_check` (`allocchk.c`, skrevs före
  allokeraren), ett syntetiskt mål (`synth.c`: N heltals- och M
  flyttalsregister, callee-sparad delmängd, x86-lika fasta register:
  tvåadress, skiftantal, div, anrop), svep 5 och 6 (`alloc.c`: linjärt läge,
  tips, loopkorrigeringar, delade spillplatser, huvudregeln, anrop,
  konstanter, parallella flyttar med cykelbrott via fritt register, `xchg`
  eller scratch), textdump och verktyget `viralloc` (`allocdump.c`,
  `viralloc.c`: dump och räknare, `--sweep`, `--time`, `--mutate`).
  Inline-asm (15.10) och `dce`-sammanslagningen ingår inte.
- **Tester:** 16 filer i `ccs/vcc/tests/vir-alloc/` med förväntade dumpar
  (rak kod, diamant, nästlade loopar, genomlevande värden, kritiska kanter,
  blockargument som byter plats och roterar, anrop, fasta x86-register,
  `ibr` till `VB_ADDR`, irreducibelt, linjärt läge); varje fil körs vid alla
  registerantal ned till minimum (3 heltal, 2 flyttal), tre callee-varianter,
  x86 av och på och linjärt läge: 618 konfigurationer, kontrollen grön på
  alla.  `bench/vir-alloc.py random`: 1000 slumpenheter (8000 funktioner,
  frö 1–600 och 1000–1299) gröna, 60 enheter med ASan+UBSan gröna;
  `mutate`: kontrollen fångar 2869 av 2979 förstörda allokeringar (missarna
  är `br`-byten som bara är information och döda resultat).
- **Fel som slumptesterna hittade, alla i irreducibelt flöde och nu regler
  i SPEC 0.5 med ett test var som faller utan regeln:** `lend` måste sättas
  vid varje bakkants mål, inte bara loophuvudet (`irr-merged.vir`); en kant in
  i mitten av en loop för med sig värdena som lever runt loopen
  (`irr-entry.vir`); ett värde som lever runt en loop med ett block som
  startar i stackplatser behöver plats (`addr-loop.vir`).  Dessutom:
  callee-sparat register som bara en redigering skriver (`x86-shift.vir`),
  tvåadress utan fritt register (första operanden kastas ut), en onödig
  självväxling efter `xchg` (`rotate.vir`).
- **Tid (bästa av fem, M1 Pro, delad maskin):** tolkformad funktion
  (`big_interp`: 2927 instruktioner, 514 block, 25 blockparametrar i
  slingan) 177 ns/instruktion vid 14+14 register, 172 vid 6+4; slumpade
  funktioner 81–83 ns.  Under budgeten 200 ns som gäller tills vidare (SPEC
  22.1, beslut 2026-11-11: kodens hastighet i sqlite först); målet 60 ns
  ligger kvar till efter kvaliteten.  Profilering gav −13 till
  −16 %: trådlokal åtkomst via pekare (`_tlv_get_addr` kostade 3–4 %), inget
  rensat registerfält per block, ingen poängsättning eller kantflytt för
  ensam föregångare, operandlistor från svep 5 till svep 6, operandfälten
  utan iterator, kölista för parallella flyttar.  Kvar: kostnad per block
  proportionell mot antalet levande värden (W byggs om, live-in-poolen,
  inträdestillstånd), utkastning (avstånd per register), `fixed()` per
  instruktion och `br` med 25 argument.  Tolkformen är värsta fallet (5,7
  instruktioner per block); siffran på riktig kod kommer med V2.
- **Räknare på testmängden** (vid varje fils översta registerantal): 27
  spill, 34 omladdningar, 17 rematerialiseringar, 15 kanter med flyttar, 2
  `xchg`, 7 callee-sparade.  Tolkformen vid 14+14: 697 spill, 1617
  omladdningar, 257 kanter med 1183 lagringar till stackplats (slingans
  parametrar startar i plats enligt huvudregeln: ett kvalitetsproblem att
  mäta mot clang i V2).
- **Mål för V4 som helhet:** `bench/sqlite/run.py all` (på en annan gren):
  speedtest1 gcc 2,55 s, clang 2,62 s, vcc 4,01 s; VDBE-tolken är den
  form allokeraren ska göra bättre.  Kan inte köras genom VIR förrän V2.
- **Det som hindrade i V1:s format:** parsern sätter inte `blk.depth`;
  `vir_cfg` skriver om `extra`/`nextra` (allokeraren anropar den varje gång);
  `vir_term_of`/`vir_succ_csr` är `VPRIV` och statiska i enhetsbygget, så
  allokeraren bygger sina egna efterföljarlistor; formatet säger inte var en
  `proj` står (allokeraren kräver att den följer sitt anrop direkt) eller att
  block 0 börjar med sina `param`; ingen loopskog.
- **Väntar på V2:** riktiga `vir_regs` för x86-64 (och arm64), sänkningen
  15.3, C som indata, kvalitet mot dagens `-O2` och clang, sqlite.  **V5:**
  `dce` i svep 5.  **V5b:** inline-asm (15.10).
- **Standardbygget oförändrat:** `bench/vbench.py check --quick` 66/66
  identiska mot ögonblicksbilden (`2d122cf1`) och mot commiten före den
  (`1327df46`); mot sammanfogningsbasen med `main` skiljer 15, från de
  osammanfogade grenarna under den här, inte från den.  `bench/vir-test.py`
  118/118.

### V4, webbar (2026-11-12): blockparameter och argument som en variabel

Steg 1 i arbetsordningen för sqlite (SPEC 15.5 och 15.8, "Webs").
- **Gjort:** en blockparameter och dess `br`-argument är en webb
  (union-find).  Två webbar med parametrar i samma block slås inte ihop.  En
  medlem som är levande där en annan definieras lämnar webben; vid en
  parameter lämnar de levande i stället.  Webben har en gemensam spillplats
  utanför ringen och ett föredraget register (den första parameterns).  En
  parameter som korsar ett anrop innan den används, eller bara skickas vidare,
  startar i webbens plats.  Livslängden runt loopar är nu exakta intervall per
  värde i stället för `lend` som linjär gräns, och `livein`, `dmark` och
  webbarnas interferens läser samma intervall.  Två precisionsregler: ett värde
  vars definition inte dominerar loophuvudet lever bara till slutet av sin
  grupp av bakkanter, inte runt hela loopen; och regeln för en kant in i en
  loop tar bara värden vars definition dominerar källblocket (strikt SSA).
  Den regeln gäller nu också en kant till huvudet (irreducibel loop där
  användningen ligger bakom en inre bakkant; `irr-head.vir`, hittad av
  slumpfrö 61, faller utan regeln).
- **sqlite3VdbeExec:** stackåtkomster 5197 → 2587 (målet var under 1500).
  Spärren är `add x25, x25, #24` på plats; dess föregångare laddade förut om
  tre värden och kopierade `pOp`.  Den största källan var regeln för kant in i
  loop: ett värde från ett fall i tolken räknades som levande runt hela
  tolkslingan.
- **Tid:** speedtest1 (storlek 100): 33,75 G → 32,48 G instruktioner, cykler
  8,83 G → 8,95 G (inom bruset; ssa-llc2 8,39 G).  Profil vid storlek 300:
  sqlite3VdbeExec 2094 sampel (före 2115; LLVM-bakdelen ssa-llc2 2228; målet
  1900 motsvarar bygget med bara inlining, 1818: kräver steg 2).
  Stackåtkomsterna kostade alltså lite: lagring och laddning på stacken
  vidarebefordras.
- **Tester:** vir-alloc 17/17, slump 1300 enheter (frö 1–1000, 1000–1299)
  gröna, `mutate` fångar 2871 av 2980; vir-test 118/118; C-sviterna 2064/2064, genom VIR med kontrollen
  2058/2064 (samma sex kända fel), inline genom VIR 85/85, `VCC_UNITY=`
  bygger, TCL genom VIR 1 fel (interrupt2, som clang); sqlite
  genom VIR med kontrollen: 1664 funktioner, hash oförändrad.

### Inliner per anropsplats (2026-11-12): storlek nedifrån och upp, ingen tillväxtbudget

Steg 2 i arbetsordningen för sqlite.
- **Gjort:** förprocessen (`prepass.c`, `fn_isize`) samlar den direkta
  anropsgrafen och räknar per funktion `isz`: den egna storleken plus `isz` för
  varje anropad funktion som själv skulle inlinas (som LLVM, nedifrån och upp).
  Rekursionsgrupper (Tarjans starka komponenter) räknas inte in och inlinas
  inte inom sig själva, så valet beror inte på trådarnas ordning.  Under VIR
  inlinas en anropad funktion när `isz <= WK_INLINE_VIR` (150 AST-noder);
  statiska funktioner med ett enda anrop som förut (4000).  Tillväxtbudgeten
  per anropare (`inlgrow`, 400) är borta: den var det som höll nere de inkapslade
  kopiorna, och den gjorde valet beroende av ordningen i anroparen.
- **Gräns** (speedtest1 storlek 100, cykler; `.text` för sqlite3.o; kompilering av
  sqlite3.c genom VIR):

  | gräns | cykler | text | kompilering |
  |---|---|---|---|
  | 40 + budget (förut) | 8,96 G | 913 KB | 0,37 s |
  | 80 | 8,91 G | 1049 KB | 0,42 s |
  | 150 (vald) | 8,83 G | 1356 KB | 0,52 s |
  | 225 | 8,79 G | 1525 KB | 0,57 s |
  | 300 | 8,78 G | 1620 KB | 0,58 s |

  Efter 150 kostar varje cykelprocent mer än 10 % kod; 150 är knät.  LLVM:s
  bygge med bara inlining (st-inl-only) går på 7,66 G, men det tar också bort
  `noinline` (`SQLITE_NOINLINE`: getCellInfo).  Profil vid storlek 300 (225 mot
  inl-only): sqlite3VdbeExec 2133 mot 1717, vdbeRecordCompareInt 641 mot 532,
  jsonTranslateTextToBlob 230 mot 129: gapet ligger i koden per funktion, inte
  i vad som inlinas.
- **Fel som inliningen visade:** walkern antog x87-layout för `long double`
  utom för `__float128`; på arm64 Linux är den IEEE binary128.  En promoverad
  `long double`-konstant lagrades i fel format (`vcc/ldouble_const` genom VIR),
  och `__builtin_isinf_sign` läste tecknet på byte 9 (`vcc/isinf_sign`, ett
  av de sex kända felen).  `wk_quad` avgör formatet på ett ställe.
- **Tester:** C-sviterna 2064/2064, genom VIR med kontrollen 2059/2064 (fem
  kända fel kvar: jump_table, switch_dispatch, switch_values, returns_twice,
  setjmp_sites), inline 85/85 med och utan VIR, vir-test 118/118,
  `VCC_UNITY=` bygger, TCL genom VIR 1 fel (interrupt2, som clang); sqlite
  genom VIR med kontrollen: 1420 funktioner, hash oförändrad.  Samma körning:
  st-web8 9,31 G cykler, nu 9,18 G (maskinen belastad; 32,50 → 32,07 G
  instruktioner).

### Allokatorns kvalitet och adresslägen (2026-11-12)

Steg 1 i den nya arbetsordningen: sampel per funktion mot virpol (VIR:s
inlining, LLVM:s bakända), inte antal stackåtkomster.  Mätt med parade
körningar (speedtest1 storlek 100, cykler).
- **Främjande av en plats med pekar- och heltalslagringar:** en lokal pekare
  vars adress en inlinad läsare tar (`sqlite3Utf8Read(&z)`) lagras som `ptr`
  och, efter `p++`, som `i64` (`p2i`, `add`, `i2p`); `vw_promote` vägrade
  fält med olika typer.  Fältet håller nu `i64`, en pekarlagrings värde går
  via `p2i`.  patternCompare:s zPattern/zString låg i minnet.
- **`i2p`/`p2i` läses igenom** före allokeringen (användarna läser
  operanden: en lasts adress blir en `add` som viks in); konstanten i en
  kommutativ operation läggs andra (en omedelbar).  −1,5 % instruktioner,
  −0,5 % cykler.
- **En kopia på 1, 2, 4 eller 8 byte** (`memcpy(&v, &r, 8)` i FpDecode) är
  en last och en lagring, så att båda platserna främjas.  −0,2 %.
- **Webbparametrar:** en webbs parameter som lever över ett anrop och inte
  läses snart gick in i webbens plats.  Nu inte när varje efterföljare (högst
  två) läser den nära sin början (−0,6 %), och inte när registret är
  callee-saved och två till är lediga vid blockets början (−2,2 %;
  patternCompare: sp-laster/lagringar 32/40 → 1/6; utan villkoret tappade
  VdbeExec:s tidigare lästa värden sina register, +1069 instruktioner).
- **Adresslägen:** VdbeExec:s gap mot virpol (+195 sampel) var inte
  stacktrafik (VIR har färre sampel på `[sp]`: 414 mot 484) utan
  instruktionsval: VIR hade inga registeroffsetadresser (LLVM 2112).  Nya
  poster `I_LDX`/`I_STX` (`[xn, wm, sxtw #k]`), `I_ADDXT`/`I_SUBXT`
  (`add x, x, w, sxtw #k`) och `I_SMADDL`/`I_UMADDL`; sänkningen
  (`vl_cinfo`/`vl_cfold`) viker ett index byggt precis före en `ptradd`,
  `add` eller `sub` (`ext.4s/4u`, `mul`/`shl` med åtkomstens storlek, `mul`
  med en 32-bitarskonstant), varje instruktion använd en gång av nästa.
  Beslutas vid kedjans första instruktion, där indexets register är känt;
  emellan bara remat/omladdning till andra register.  Laster/lagringar:
  −2,0 % instruktioner, −1,1 % cykler; add/sub/smaddl: −2,5 %
  instruktioner, cykler oförändrade (samma latens som det de ersätter).
- **Summa:** mot steg 3, samma körning: −7,3 % instruktioner, −3,5 %
  cykler; virpol är nu 9,7 % snabbare (var 12,9 %).  sqlite .text 338547 →
  312681 instruktioner, spill 5684 → 3753.
- **Kvar, mätt:** sampel på `[sp]` i mellanstora funktioner (vappendf 95 mot
  38, BtreeInsert 54 mot 16); f64 som lever över anrop laddas om via x16
  (inga callee-saved FP-register: `vl_regs` sätter `callee[1] = 0`, 107
  ställen); konstanter rematerialiseras i loopar (ingen hissning).
- **Tester:** `vcc/ptr_slot_inline.c`, `vcc/memcpy_small.c`,
  `vcc/index_addr.c`; C-sviterna genom VIR med allokeringskontrollen, vcc-cfi
  och darwin -S genom VIR (kodningen lika med clangs assemblering av texten).

### Registerkopior kring anrop (2026-11-12)

Uppdrag från jämförelsen mot virpol: `mov` mellan register låg +313 sampel
över LLVM (2,0x statiskt).  Exemplet (`mov x21, x0; b join` i varje gren av
vdbeRecordCompareString) var ingen blockparameter: `v3 = trunc v0` är bunden
(i operandens register när den dör) och stannade i x0, fast den lever genom
en loop med ett anrop; vid sammanflödet låg den i x21, så varje gren och
bakkanten flyttade den.  Tre regler i `vir/alloc.c`:
- en bunden operation vars resultat lever över ett anrop i en loop räknas
  inte på plats i ett caller-saved register när ett callee-saved är ledigt
  (en flytt vid definitionen i stället för en per varv);
- en `ext` (en instruktion i vilket register som helst) tar sin hints
  register när det är ledigt (alla bundna: fler flyttar, prövat);
- ett värde vars nästa användning har fasta operander (ett anrops argument)
  väljer, när hinten är upptagen, inte ett register som en annan av dess
  operander ska till: memcmp:s `mov x3, x1; mov x1, x2; mov x2, x3` blir
  `mov x1, x3`.  En fallback till blockparameterns egen hint när den saknar
  tentativt register prövades: fler instruktioner, struken.
sqlite: `mov` mellan register 37904 → 35785 (statiskt, hela binären),
instruktioner 312681 → 310218; parat mot föregående bygge −0,1 % instruktioner, −0,6 %
cykler (inom bruset för var regel för sig).  Kvar av de heta flyttarna, ur
profilen: väljaren i en `switch` kopieras till x17 före jämförelsen
(VdbeExec:s heta dispatch, vdbeRecordCompareInt, SerialGet) och `mov w0, w0`
före en `ret` (sänkning, lämnat till vir-lowering); en rotation av fyra
loopparametrar på en bakkant i VdbeExec; resten är sampel efter hopp.
- **Lever över ett anrop, efter levande mängderna:** `XCALL` var ett test i
  instruktionsordning (ett anrop mellan definitionen och sista användningen),
  så argumenten till anrop i skilda grenar (vdbeRecordCompareInt:s v1/v2)
  räknades som levande över anrop och flyttades till x19/x20.  Nu märks vid
  varje anrop värdena i W (levande efter det); värden som lever runt en loop
  (utanför W bortom bakkanten) och linjärt läge behåller ordningstestet.
  sqlite: −1074 instruktioner, −474 `mov`, 17 ramar färre, spill 3787 →
  3727; parat −1,5 % instruktioner, −0,4 % cykler.

### Syskonanrop, fritt argumentbyte och va_arg (2026-11-12)

Steg 3 i arbetsordningen för sqlite.
- **Syskonanrop:** ett anrop vars resultat (eller inget) är vad `ret` efter
  det returnerar blir ett hopp efter epilogen, var som helst i kroppen (VIR:s
  sänkning, `vl_sibling`).  Resultatet får gå via `br` till ett block med bara
  `ret`, via blockparametrar, `i2p`/`p2i` och, när funktionen returnerar ett
  32-bitars heltal, `trunc`/`ext.4` (övre halvan är ospecificerad).  Inte när
  en plats adress lämnar laster, lagringar och kopior (som LLVM:s
  TailCallElim), med stackargument, asm, `va_start` eller ett anrop som
  returnerar två gånger; attributen som för arm64-emittern
  (`em_tail_caller_ok`, `em_tail_target_ok`).  `I_TAIL` cond 2/3 expanderas
  av `finish_function` till återställning, `mov sp, x29`, `ldp` och `b` eller
  `br x16`; med avvecklingstabeller mellan `.cfi_remember_state` och
  `.cfi_restore_state` som clang.  sqlite: 481 direkta och 51 indirekta.
- **Fritt argumentbyte:** ett värde som dör vid ett anrop och som ett fast
  argumentregister tränger undan får nu ett register som anropet skriver
  över, och tas bort helt när dess kopia redan står i ett tidigare
  argumentregister.  Förut fick det ett callee-saved register, vilket gav en
  ram i funktioner som getCellInfo.  sqlite: spill 6991 → 5565.
- **Tidig retur före prologen:** getCellInfo har nu ingen ram alls (`ldrh`,
  `cbnz`, ..., `br x16`; returen är `ret`).  Allmän shrink-wrapping (en
  funktion som behöver ram på en väg) är inte gjord.
- **va_arg och variadiska funktioner (Apple):** `vw_va_arg` flyttar pekaren
  (8 byte per argument, 16-justerade först, poster över 16 byte via
  referens), `va_start` blir `add x16, x29, #16+namngivna stackbyte; str`.
  `wk_valist` tog en pekartypad va_list för x86-64:s array; Apples är en
  `char *` (lvärde).  AAPCS64 (Linux) vägras som förut.  sqlite: 19
  variadiska funktioner och sqlite3_str_vappendf går nu genom VIR (1432
  sänkta, 6 kvar: poster som argument).
- **Tid:** speedtest1 storlek 100, samma körning: steg 2 9,38–9,42 G cykler,
  med syskonanrop och argumentbytet 9,28–9,30 G (−1,3 %), instruktioner
  32,08 → 31,64 G.  va_arg-täckningen ändrar inget mätbart (9,32 G).
- **Tester:** `vcc/tail_calls_body.c` (syskonanrop i en gren med
  callee-saved register, via `br`, indirekt, pekare; negativt: `(unsigned)`
  breddat till 64 bitar, lokal adress), C-sviterna, vcc-cfi genom VIR
  1567/1567, vcc-abi `--host` genom VIR samma fyra kända fel som utan.

### Sänkningen mot LLVM:s backend (2026-11-12): fem punkter, sqlite speedtest1

Gren `claude/vir-lowering-453a57`.  Mätt med `VCC_VIR=bl`, speedtest1
`--size 100 --memdb`, parvisa körningar (3 rundor) mot bygget före punkten och
mot virpol; räknare statiskt på sqlite3.o.  Från start (5346e3e6) +14,5 %
cykler mot virpol, nu +9,9 %.  Samplingar per funktion (storlek 300) varierar
±10 % med maskinens last och redovisas bara som riktning.

| Punkt | Commit | Räknare före → efter (virpol) | Instr. | Cykler mot föregående | Mot virpol |
|---|---|---|---|---|---|
| 1 grenval: tbz/tbnz, cbz/cbnz mot noll och tecken, upprepad jämförelse bort | 71ccdf3c | tbz+tbnz 0 → 2677 (2645), and+cbz 2690 → 497, cmp 13976 → 13072 | −3,0 % | −1,9 % [−2,1 .. −1,6] | +12,0 % |
| 2 blockordning och utgångar: retur med egen epilog, ensamma `br` kopieras, lövram utan ramrekord | ad813ee8 | b 20213 → 18915 (12145), ret 1473 → 2771 | −0,7 % | −0,9 % [−2,5 .. −0,4] | +11,0 % |
| 3 aritmetik: registeroffset, skiftade/utökade operander, madd/smaddl, ubfx/sbfiz, multiplikation med konstant | dabc8f08 | registeroffset 0 → 2567 (2675), utökade 0 → 1023 (1283), smaddl 0 → 1753 (1855), sxtw 5509 → 1252 (1629), mul 2892 → 383 (228) | −5,6 % | −1,2 % [−2,7 .. −0,9] | +8,1 % |
| volatile genom pekare (förutsättning för 4) | 5c4313d5 | dekkerMul2 ldr/str 9/6 → 29/13 (21/12) | +0,4 % | +2,5 % [+2,0 .. +3,1] | |
| 4 bytesammansättning: en last + rev/rev16; anrop läser bara sina argumentregister | b72162c6 | rev 0 → 100 (186), rev16 0 → 65 (41) | −1,2 % | −0,8 % [−1,2 .. +0,1] | +9,7 % |
| 5 konstanter: wzr/xzr-lagring, add/sub/cmp- och logiska immediater | a4d0daec | lagrad nolla via register 1722 → 333 (132), konstantflytt 24294 → 22602 | −0,3 % | +0,1 % [−0,2 .. +0,3] | +9,9 % |

- Punkt 2: kopior av små block med arbete i (slingtestet sist i latchen)
  prövades och togs bort: fler instruktioner (konstanter och kantflyttar
  sänks en gång per kopia).
- Punkt 3–5 är ett pass över posterna (`opt.c` `fold`, bara VIR:s
  funktioner, exakt liveness): definitionen går när dess register bara läses
  av nästa läsare i blocket och är dött efter den.  VIR:s retur läser bara x0
  för heltal/pekare ≤ 8 byte.
- Volatile: VIR byggde varje last/lagring som icke-volatil; rättningen
  kostar cykler eftersom VIR:s flyttalslaster går via x16 + `fmov`.
- Kvar per funktion: VdbeExec (slingtest roteras inte, hopptabell med 4-byte
  poster mot LLVM:s 1 byte), RecordCompareInt/String (parametrar kopieras till
  callee-saved x19/x20 fast bara syskonanrop följer: allokeraren), SerialGet
  (LLVM inlinar sqlite3Get4byte), csel används inte alls (0 mot 980).
- Till allokeraren: parametrar till callee-saved före syskonanrop (rättat på
  `claude/vir-regalloc-impl-f22a1d`: över-anrop-testet går på levande
  mängder).  Konstanter som en postvikning tar upp ska i stället markeras i
  `vl_fix` (mem-biten) så att de aldrig får register: sänkningens sak.
- Ombaserat på allokeringsgrenen (eab5ebd0): en uppsättning poster,
  dess `I_LDX`/`I_STX`/`I_SMADDL`/`I_UMADDL`; `I_ADDXT`/`I_SUBXT` blev
  `I_ADD`/`I_SUB` med cond 8 + option (som skiftsorterna).  Parvis, storlek
  100: mot eab5ebd0 −6,8 % instr., −2,7 % cykler; mot virpol +6,8 % cykler;
  mot clang -O2 +10,3 % (virpol mot clang -O2 +3,9 %).
- Efter ombasering på 5ae88996 (allokerarens över-anrop-test): låg halva
  (32-bitars retur, int-argument, genom blockparametrar) ger ingen
  `mov wN, wN` (724 → 56 i sqlite3.s), switch indexerar tabellen med
  väljaren själv (2e4ad9d8: −0,7 % instr., −0,1 % cykler); multiplikatorer
  som `fmulk` viker får inget register (`vl_fix`: −0,4 % instr., cykler inom
  bruset).  Mot virpol +5,5 % [+5,1 .. +5,7], mot clang -O2 ca +10 %.
  Kvar här: `x * -1 + a` blir madd med −1 i stället för sub.
- If-konvertering i byggaren (`vw_ifconv`, före `vw_split`): en gren vars armar
  är tomma eller har högst två billiga instruktioner utan effekter och som
  skiljer sig i ett värde vid sammanflödet blir `select`.  sqlite3.s: csel
  0 → 984 (LLVM 980), villkorliga hopp 30360 → 29372, 438 färre
  instruktioner statiskt; parvis +0,2 % instr., cykler −0,1 % [−0,8 .. +0,3]
  och −0,9 % [−1,6 .. −0,3] i två körningar (inom bruset).  Mot virpol
  +5,5 %, mot clang -O2 +10,0 %.  Kvar: konstanter i csel (csinc/wzr),
  `and` + `subs` före csel som `tst`.
- **Granskning (2026-11-12) och rättningar.**  Tre felkompileringar från den
  här grenen, alla i postrensningen (`opt.c`), var och en med test som
  faller utan rättningen (a51b4106): `exts` läste add med utökad operand som
  vanlig 64-bitars add (`vir_ext_add_facts.c`); `flags` tog bort en jämförelse
  vid en etikett som ett senare hopp går tillbaka till
  (`vir_flags_backedge.c`); `fold` flyttade en läsning av x16/x17 förbi en
  post som bygger om dem (`vir_scratch_fold.c`).  De äldre passen är inte
  utsatta (bara x0–x15/x19–x28).  `returns_twice` och `setjmp_sites` gick i
  tidsgräns genom VIR på grund av `flags`-felet: kända fel 5 → 3.
  **Mätvärdena ovan från punkt 1 och framåt är för bra**: felet träffade
  `sqlite3_str_vappendf` (en jämförelse vid ett slinghuvud saknades, hashen
  stämde ändå).  Rättat: +2,2 % instr., +2,7 % cykler [+2,5 .. +2,7]; mot
  virpol **+7,9 %** [+7,1 .. +8,6].
- Volatile genom VIR (8dceb271): lokaler, bitfält, initierare och döda
  lagringar; `bench/vcc-volatile.py` 32/32 (25/32 före), inte i CI än
  (bara kört på macOS).
- Låghalvssvepet går bakifrån (b096c081): 4000 kedjade `if` 0,57 s → 0,02 s.
- Granskningens öppna punkter, väntar på beslut: if-konvertering som pass
  över färdig VIR i stället för i byggarens för-IR; 32-bitars argument och
  resultat i SPEC-vir.md (O1 säger att byggaren utökar, sänkningen antar
  motsatsen); SPEC 15.3/15.4/1.3 mot koden (switch, svansanrop, vikningar).
  Kompileringstid: `fwriter` är nu begränsad; `tbz_relax` kodar varje post i
  funktioner med en tbz; byggaren nollar sin hashtabell i största storlek per
  funktion (~6 % av sqlite).
- Tester: `vir_branch_bits.c`, `vir_flags_reuse.c`, `vir_layout_exits.c`,
  `vir_operand_fold.c`, `vir_byte_assembly.c`, `vir_constants.c` (.o och .s).
- **Flyttalslaster och -lagringar direkt (2026-11-12).**  Nya poster `I_LDF`/
  `I_STF` (rd är ett v-register, sz 2/3, `[rn + imm]`, också mot FRAME):
  `ldr d2, [x0]` i stället för `ldr x16` + `fmov`.  Används av VIR:s last och
  lagring (inte symbol- eller registeroffsetadresser än), spill/återladdning,
  stackargument och stackparametrar.  Postrensningen: bara basregistret läses;
  en ramplats som en sådan post rör räknas som undkommen i platsvärdespasset,
  och `I_STF` är en lagring för platsvärden och bytesammansättningen.
  sqlite3.o: `fmov` via x16/w16 487 → 114, `.text` −1512 byte;
  dekkerMul2 89 → 59 instruktioner (clang ca 44 per inlinad kopia).
  speedtest1 storlek 100, parvis 4 rundor på en lastad maskin: instruktioner
  −0,45 % (28,65 → 28,52 G), cykler −0,5 .. −3,5 %.  Utan VIR är utdata
  byte-identiska (`vcc-same.py`).  Test `vir_fp_mem.c`; C-sviterna genom VIR
  samma tre kända fel (`.Lswitch.values`), vcc-abi (Linux 34/34, `--host`
  samma fyra), vir-test 119/119, vcc-volatile 32/32, sqlite TCL genom VIR
  1 fel (interrupt2, som clang).  Kvar i dekkerMul2: inlining i anroparna,
  `fmadd`, lövram utan ramrekord, `[x0, #8]` i stället för `add` + `[x1]`.
- **`fma` (2026-11-12).**  Ny VIR-instruktion 9 `fma a, b, c` (72–79 är vektoroperationerna) (flaggor `negp`,
  `negc`; SPEC-vir.md), sänkt till fmadd/fmsub/fnmsub/fnmadd.  Byggaren
  kontraherar som clangs `-ffp-contract=on`: en produkt som uttrycket självt
  just räknade (namnet håller den fortfarande; också negerad) adderas eller
  subtraheras, vänster operand först; inte en produkt i en variabel, värdet av
  en tilldelning, en konverterad produkt, en via `?:` eller en konstantprodukt.
  Negerade faktorer och addender blir flaggor.  `-ffp-contract=off` stänger av
  (`cc_opts.no_fp_contract`); `#pragma STDC FP_CONTRACT` läses inte, och den
  direkta emittern kontraherar inte.  Döda flyttalsoperationer tas nu bort i
  byggaren (statusflaggor bevaras inte, som clang utan FENV_ACCESS).
  39 provuttryck mot clang -O2: samma operationer utom där värdet ändå är
  lika.  sqlite3.o: 13 fmadd-familjen (clang 46, varav 36 i de 12 inlinade
  dekkerMul2); dekkerMul2 59 → 57 instruktioner med clangs tre fmadd;
  speedtest1 −0,05 % instruktioner (flyttal är en liten del).  Test
  `vir_fp_contract.c` (alla former fusionerade eller ingen; de clang inte
  kontraherar förblir okontraherade); C-sviterna genom VIR samma tre kända
  fel, utan VIR 2082/2082 och byte-identiskt; vir-test 119/119, vir-exposure
  56/56, virgen 101/101, vcc-abi, vcc-driver 212/212, sqlite TCL genom VIR
  1 fel (interrupt2).
  Efter sammanslagningen med vektoriseraren (main 75bb9062): `fma` tar också
  vektortyper (fmla/fmls, addenden negerad först för `negc`) och
  vektoriseraren känner den, annars föll TSVC från 43 till 32 vektoriserade
  slingor; nu 43 av 120, 151/151 kontrollsummor.  Mains direkta
  flyttalslaster (`vl_fmem`, ord) går först; `I_LDF`/`I_STF` tar ramplatser,
  volatila åtkomster, spill och stackargument.  sqlite genom VIR mot main:
  304176 → 303889 instruktioner statiskt, speedtest1 28,63 → 28,53 G.
- **ccmp (2026-11-12).**  Byggaren (`vw_condmerge`, före if-konverteringen)
  slår ihop två grenar mot ett gemensamt mål som LLVM:s
  FoldBranchToCommonDest: `brif c1, S, O` där S bara håller en heltalsjämförelse
  c2 och grenar på den med O som ena mål blir `and`/`or` av c1 och c2 (c2
  inverterad vid behov) och en gren; upprepat ger kedjor.  Jämförelser som ensamma
  är cbz/tbz (mot 0 och -1) lämnas.  Sänkningen (`vl_chain`, `vl_ccmp`): icmp,
  sedan (icmp, and/or) en eller flera gånger, sedan brif eller select, allt läst
  en gång och i rad, blir cmp + ccmp/ccmn (omedelbart 0..31, -1..-31, annars via
  x17) och inget sanningsvärde i register.
  **Mätt, och därför snålt påslaget:** att slå ihop varje grenpar (`VCC_CCMP=all`)
  ger på sqlite 380 ccmp (clang 552) men speedtest1 +0,4 % instruktioner (den
  andra jämförelsen körs också när den första avgjorde) och oförändrade cykler
  (median 9,10 mot 9,09 G, 8 parvisa körningar).  Mikrotest (4096 par, 60 000
  varv): oförutsägbara data 1,45 → 1,18 G cykler (−19 %); första villkoret
  nästan alltid falskt +1 % cykler, +2 % instruktioner; nästan alltid sant och
  ett slingburet värde 1,98 → 2,47 G (+25 %: den sammanslagna grenen
  if-konverteras till csel på beroendekedjan; clang gör samma val).  Utan
  profil är det alltså en chansning.  Standard är därför bara fallen där
  if-konverteringen kan följa (resultatet är ett värde: `return a == 1 && b
  == 2`, `x = c1 || c2 ? p : q`): färre instruktioner och inga grenar.  sqlite:
  19 ccmp, −18 instruktioner statiskt, speedtest1 −0,1 % instruktioner.
  `VCC_CCMP=off|all` för mätning.  Kvar att pröva: slå ihop grenpar bara när
  de är dåligt förutsagda (kräver profil), kedjor av tre i värdefallet.
  Test `vir_ccmp.c` (alla villkorskoder × and/or × 32/64 bitar, omedelbara,
  kedjor, select) under clang, vcc, VIR val/all/off; C-sviterna genom VIR i
  båda lägena samma tre kända fel; utan VIR 2083/2083 och byte-identiskt;
  vir-test, virgen, vir-exposure, vcc-abi, vcc-volatile, vcc-stress; sqlite
  hash i alla tre lägena, TCL genom VIR 1 fel (interrupt2).

### zlib: blockparameterns register röstas fram av föregångarna (2026-11-12)

`bench/zlib/run.py` (zlib 1.4, zbench på zlibs egen källtar).  Profil mot clang
-O2: `longest_match` är hetast och var 5 % långsammare.  Orsak: slingans
vanliga väg (första jämförelsen missar) skickar `scan` och `best_len`
oförändrade till blocket före bakåtkanten, men parametrarna där fick
registren från den sällsynta vägen (`treg`: första argumentet som placeras),
så varje varv gjorde fyra `mov` och två extra hopp.
- **Åtgärd** (`alloc.c`, `arg_vote`): en blockparameter tar det register där
  flest redan placerade föregångare har sitt argument (minst två, och fler än
  i `treg`; 3–32 föregångare).  Den sällsynta vägen får flyttarna.
- **Resultat:** den heta slingan 16 → 10 instruktioner (clang 12).  zbench mot
  clang: deflate-9 1,09x → 1,05x, totalt 1,10x → 1,08x; `.text` 79896 → 79860.
- **Kvar** (efter nästa avsnitt): `slide_hash` (clang vektoriserar), `crc32_z` (inline
  asm, går inte genom VIR).
- Verifierat: vir-alloc 17/17, random 300, mutate, vir-test 119/119;
  vcc-ctests med `VCC_VIR=bl VCC_VIR_CHECK=1` 2077/2080 (de tre felen,
  `.Lswitch.values`, finns före ändringen); inline 85/85; sqlite3.c genom VIR
  med kontrollen; zlib-testerna ger clangs byte.

### zlib: en laddning återanvänds över block och slingor (2026-11-12)

Byggarens värdenumrering (`vw_simplify`) återanvände en laddning bara inom ett
minnestillstånd: det bröts vid varje sammanflöde och varje lagring, och adressen
(`ptradd`) numreras per block, så `s->window` lästes om i varje varv av
`longest_match` trots att den lästs före slingan.
- **Åtgärd** (`build.c`, `vw_far`): en andra tabell håller varje laddning per
  bas + konstant offset för blocken den dominerar.  En senare laddning av samma
  byte blir det värdet när inget däremellan kan skriva dem; "däremellan" är
  positionerna i omvänd postordning efter den första fram till den andra, eller
  till slutet av den yttersta slinga som håller den andra men inte den första
  (resonemanget står i kommentaren).  En lagring till samma bas på ett annat
  offsetintervall skriver inte; annan bas, anrop, `memcpy`, atomära och asm
  skriver allt.  Högst 32 skrivare granskas per laddning.  Ingen laddning
  flyttas eller läggs till: en slingas laddning försvinner bara när samma
  laddning redan finns i ett dominerande block (äkta LICM med förhuvud kvarstår).
- **zlib** (zbench mot clang -O2): totalt 1,08x → 1,03x, deflate-9 1,05x →
  1,00x, deflate-6 1,14x → 1,09x; deflate.c 1789 → 1553 laddningar, `.text`
  79860 → 78304.
- **sqlite** (genom VIR): laddningar 67304 → 65311, text 1217388 → 1209808,
  speedtest1 --size 100 CPU 2,785 s → 2,808 s (+0,8 %, inom bruset; fler värden
  lever över block), kompilering av sqlite3.c 0,632 → 0,643 s.
- Också rättat: `vw_simp1` räknade `k + 1` med tecken för masken `INT64_MAX`
  (UBSan, nio tester i vcc-sviten under `build/ccs-asan`).
- Test: `tests/vcc/vir_load_far.c` (lagring efter laddningen i kroppen, genom
  annan pekare, överlappande smalare och bredare, anrop, inre slinga, en arm,
  goto-slinga med ingång i mitten, variabelt index, flydd lokal).
- Verifierat: vcc-ctests med `VCC_VIR=bl VCC_VIR_CHECK=1` 2077/2080 (samma tre
  `.Lswitch.values` som förut) och under ASan+UBSan (utöver dem två fel i
  framänden, `gcc/eeprof-1` och `gcc/pr105613`, inte i VIR); inline 85/85;
  vir-test, vcc-volatile; sqlite3.c under ASan; zlib ger clangs byte; sqlite
  speedtest1 samma verifieringshash.

### zlib deflate-1: fältadresser, smala lagringar, sub+cmp, csel med noll (2026-11-12)

Profil av nivå 1 mot clang (sampel, 60 varv): `fill_window` 229 mot 11 (clang
vektoriserar `slide_hash`), `deflate_fast` 596 mot 422, `longest_match` 838 mot
722, `compress_block` 389 mot 322.  Fyra allmänna brister i koden åtgärdade:
- **Fältadress per åtkomst** (`build.c`, `vw_ownaddr`): numreringen ger ett
  blocks åtkomster till samma fält en delad `ptradd`, och sänkningen viker bara
  en `ptradd` med en användare direkt efter.  Varje `load`/`store` med bas +
  kodbar konstant (−256..255, eller multipel av bredden upp till 4095) får nu
  sin egen kopia precis före sig: `add x1, x19, #160; ldr w2, [x1]; …; str w2,
  [x1]` blir `ldr w2, [x19, #160]; …; str w2, [x19, #160]`.
- **Smal lagring** (`vw_simp1`): `store` läser bara de låga byten, så värdets
  `ext` (minst så bred) eller `trunc` hoppas över (`ubfm` före `strh`/`strb`).
  Inte för lagringar till befordrade platser: deras operander räknas inte.
- **sub + cmp** (`opt.c`, `flags`): en `sub` direkt före en `cmp` av samma
  operander blir `subs` och jämförelsen går.
- **csel med noll** (`a64.c`): en nollarm läses ur `wzr` i stället för att
  materialiseras.
- **Resultat:** `slide_hash`-slingan 10 → 7 instruktioner; deflate.c 5586 →
  5219 instruktioner; zlib `.text` 78304 → 74436.  zbench mot clang -O2:
  deflate-1 1,36x → 1,19x, deflate-6 1,09x → 1,02x, totalt 1,03x → 1,02x.
  sqlite genom VIR: 292365 → 287144 instruktioner, text 1209808 → 1188920,
  speedtest1 CPU 2,742 → 2,731 s (2,768 före dagens tre ändringar).
- **Kvar på nivå 1** (kräver beslut): `slide_hash` behöver vektorisering
  (clang: NEON `uqsub`); `deflate_fast`/`compress_block` läser om `s->`-fält
  efter varje lagring genom `s->head`/`s->prev` (`ush`), vilket clang slipper
  med typbaserad alias, som VIR enligt beslutet 2026-11-11 inte har utan flagga.
  Utan beslut: lagring → laddning över lagringar till samma bas på annat offset
  (`str w2, [x0, #8]; str w1, [x0, #12]; ldrsw x1, [x0, #8]`).
- Test: `tests/vcc/vir_field_access.c`.  Verifierat: vcc-ctests `VCC_VIR=bl
  VCC_VIR_CHECK=1` 2079/2082 (samma tre), med verifieraren (`blv`) vcc-sviten,
  inline 85/85, direkta emittern 2082/2082, ASan+UBSan som förut, vir-test,
  vcc-volatile, sqlite3.c med kontrollen och samma verifieringshash, zlib ger
  clangs byte.

### Kompileringstid genom VIR (2026-11-12): sqlite3.c

Gren `claude/vir-sqlite-optimization-acaf18`.  Mätt på sqlite3.c `-O2`
(amalgamationen, 1438 funktioner genom VIR), instruktioner för hela
`vpp`-processen (maskinen är delad: tider är minimum av nio körningar).

| | Instruktioner | Emit, 1 tråd | Emit, 8 trådar |
|---|---|---|---|
| direkt emitter | 1,29 G | 57 ms | |
| VIR före | 4,84 G | 438–540 ms | 90 ms |
| VIR nu | 3,75 G | 409 ms | 62 ms |
| clang -O2 (hela kompileringen) | | ca 8–10 s | |

- **Gjort (ac6639cd), samma objekt byte för byte:** `vw_promote` har fältlistor
  per plats (var kvadratisk: varje åtkomst sökte funktionens alla fält, varje
  lagring gick igenom dem igen); (block, variabel)-hashen har generationer i
  stället för rensning per funktion och börjar om på 1024 poster per funktion
  (granskningens punkt "rensning i största storlek", ~6 % av sqlite);
  etikettabellen rensas bara så långt den användes; `vw_local` slår upp
  (symbol, instans) i en hash; `vir_oit_init` läser fältmasken ur en tabell.
- **Trådar:** `VCC_THREADS=8` ger 62 ms emit (6,6 gånger mot en tråd), vilket
  är i nivå med direkta emittern på en tråd.  Standard är fortfarande en tråd
  (kärnbygget parallelliserar över TU:er); beslut behövs om en ensam stor TU
  ska få fler.
- **Fel som trådarna visade (b236ef95):** allokerarens `dmark` lästes för värden
  som ännu inte använts i funktionen, alltså en tidigare funktions stämpel:
  med flera trådar skilde sqlite3.o mellan körningar (16 funktioner).  Nollas
  nu vid varje definition; 26 funktioner ändras även på en tråd.  Emit-trådarna
  fick Darwins standardstack (512 KB): `tests/vcc/limits.c` gav inget objekt
  på 8 trådar; de får nu den skapande trådens storlek.
- **Öppet, inte VIR:** `tests/vcc/deep_expr.c` och `deep_mixed.c` ger olika
  objekt mellan körningar med `VCC_THREADS=8` också på direkta vägen (utan
  `VCC_VIR`).  Inte utrett.
- **Kvar i profilen (platt, ingen post över 6 %):** `vw_hslot` 6 % (Brauns
  uppslag per (block, variabel); cachemissar i VdbeExec), `vw_promote` 4,5 %,
  `vw_find` 4 %, `vir_oit_next`/`init` 5 %, `vw_simplify` + `vw_cse_blk` 4 %,
  allokeraren (`instr`, `sweep5`, `conform`, `enter`) ca 8 %, postrensningen
  (`opt_function`).  Nästa steg med mätbar effekt: operanditeratorn inline i
  `vir.h` (anropas över TU-gränsen), triviala phi bort direkt (granskningens
  första rekommendation), `vir_canon` utan dubbla dominatorer.

Kodkvalitet på samma gren (519b6905): flyttal laddas och lagras utan
allmänt register också för volatile, ramplatser och spill (sqlites
`dekkerMul2` var 2,9 % av speedtest1); `__builtin___memcpy_chk` m.fl. med
objektstorlek som inte kan överskridas blir det vanliga anropet också när
längden inte är konstant (530 `_chk`-anrop i sqlite).  speedtest1 storlek 100:
8,76 → 8,56 G cykler (clang -O2 7,83 G), samma verifieringshash.  Största
kända rest mot clang: adresstagna lokaler i slingor utan anrop (`balance`:
`b.nCell` laddas och lagras varje varv; kräver aliasanalys på typ eller
ursprung, ett beslut för SPEC-vir.md).

### Underlag för V4 (2026-11-11): registerallokering, ingen kod

- `RESEARCH-regalloc.md`: vad GCC (IRA, LRA) och LLVM (Greedy, Fast, PBQP,
  MLGO) gör och var de är svaga; SSA-baserad allokering (kordala grafer,
  Braun–Hacks spillning, tips i stället för sammanslagning); linjär
  genomsökning och dess efterföljare (C1, Go, V8, JavaScriptCore, regalloc2,
  TPDE, LuaJIT, QBE); optimala och inlärda metoder; jämförelsetabell.  Källa
  per påstående, obekräftade punkter märkta.
- **Svar på frågan "kan vi göra bättre än clang och gcc?":** på
  kompileringstid ja, med stor marginal (två linjära svep i stället för
  intervallträd och prioritetskö); på kodkvalitet nej i allmänhet.  Bättre
  kod bara på några bestämda ställen: asm-alternativ (`"rm"`) avgörs i
  allokeraren, valet av callee-sparat register görs med anropens läge känt,
  och felklasserna kring delregister och tvåadressomskrivning före
  allokeringen finns inte.  LLVM och GCC ligger före i delning styrd av
  blockfrekvens, femton års specialfall och shrink-wrapping.
- **Rekommendation (SPEC-vir.md utkast 0.4, avsnitt 15):** inte linjär
  genomsökning över intervall.  Svep 5 (bakåt, tillsammans med `dce`) bygger
  en stack med nästa användning per operand och live-in-listor per block;
  svep 6 (framåt) tilldelar register girigt, följer tips och kastar ut värdet
  med längst till nästa användning.  Ett värde med stackplats lagras en gång,
  efter sin definition.  Antalet svep är oförändrat (7).  Reservalternativ om
  kvalitetsgrinden i V7 faller på allokeringen: en bakåtspårande allokerare
  i regalloc2:s stil bakom samma gränssnitt.
- **Osäkert:** inga egna mätningar (alla tal är andras); kvalitet under
  registertryck mot dagens Greedy är opublicerad; regeln för irreducibla
  grafer är härledd här och måste täckas av `vir_alloc_check` och `fuzz-vir`;
  budgeten 60 ns per VIR-instruktion för svep 5 och 6 är ett förslag.
- **Mäts i V4:** `vbench.py ab emit` med `VCC_VIR=1`, statiska instruktioner
  mot dagens `-O2`, clang och gcc, räknarna i `VCC_VIR_STATS`, och samma
  TU-mängd genom clang med `-mllvm -regalloc=basic` för att se hur mycket
  allokerarens kvalitet alls är värd.

### V1 verifier: säker och linjär slot-exponering

Den tidigare analysen avbröt efter 1 000 helsvep och kunde då godkänna en
felaktig `exposure`-deklaration. Ett giltigt dominatorordnat exempel med
1 003 bakåtpekande blockargument lagrar en slotadress men godkändes utan
`exposed`; fem länkar avvisades korrekt. Analysen går nu baklänges från
exponerande användningar via `ptradd`, `select` och blockargument en gång
för hela funktionen. Arbetet är linjärt i värden, operandanvändningar och
slotar; en kontrollerad allokering håller temporära kanter och kö. Kan minnet
eller indexen inte representeras misslyckas verifieringen, aldrig med ett
obevistat godkännande. Funktioner utan `exposure` eller slotadresser bygger
ingen sådan graf.

56 genererade fall täcker långa exponerade och oexponerade kedjor, cykler,
flera slotar och addender samt deterministiska grafer; den ursprungliga
verifieraren misslyckar det långa regressionsfallet. Befintliga VIR-prov
118/118 och de nya 56/56 passerar i unity, separatfilbygge och ASan/UBSan.
Ytterligare 205 små grafer gav exakt samma verifierarresultat som basen.
VIR-proven körs nu även av arm64- och ASan-premergejobben. Lokal full ASan-
premerge passerar (en befintlig vccd-peer-kontroll saknar lösenordsfri sudo).

Sju växelvisa `perf stat`-rundor för hela VIR-provkorpusen, inklusive Python-
driver och 118 barnprocesser, gav +0,006 % user-instruktioner, +0,46 %
user-cykler och +0,15 % task-clock; korta sampel på delad värd visar ingen
säker allmän tidsförändring. På syntetiska oexponerade kedjor med 5, 64 och
1 003 länkar minskade `virtest` user-instruktioner 1,6 %, 35,8 % och 90,4 %;
med 5, 64 och 1 000 slotar minskade de 0,9 %, 28,3 % och 87,6 %. Dessa är
hela verktygsprocesser och inte `-O2`-kompilering. Proveniens finns i
`work/opt-profile/vir-exposure-backward/`. Ändringen länkas bara till virtest:
32 av 34 gemensamma objekt är byteidentiska mot basen; `vir.o` ändras och
`ppdriver.o` innehåller worktree-specifik include-sökväg. Åtta riktade
standardutdata är identiska. `vbench.py check --quick` kunde inte slutföras
här: workträdets `lex-files.txt` saknas.

Separat tidigare fel på basen: en tom VIR-blockkropp ger UBSan i
`parse.c:1839` (nollängds `memcpy` med nullkälla). Mutation seed 42 gav
998/1000 utan sanitizerfel; båda träffarna har samma basfel. Minsta indata
finns i `work/opt-pr-tmp/vir-empty-block-ubsan.vir`. Parserfelet ingår inte i
exponeringsändringen; V1:s sanitizer-/fuzzgrind är därför fortsatt öppen.

### ffmpeg som testprojekt (2026-11-12)

`bench/ffmpeg/run.py` (gren `claude/rust-compilation-speed-research-ff36c4`):
ffmpeg n8.1.3, macOS arm64, `--disable-asm --disable-autodetect`, `-O3`, så
att DSP-kodens C-varianter är det som körs.  Alla träd konfigureras av clang
(samma `config.h`); vcc kompilerar alla 2019 C-filer, clang länkar.

- **Korrekthet:** inga filer föll tillbaka på clang.  FATE utan samples: 2740
  av 2740 för clang, direkta emittern och VIR.  Ett fel hittat och rättat
  (b44a3918): variabla argument till biblioteks-builtins fick inte
  standardbefordran (`float` till `double`), 23 FATE-fel.  Med
  `-ffp-contract=off` ger alla 21 arbetslaster i `speed` clangs byte från båda
  vägarna; med kontraktion skiljer fyra flyttalslaster (andra fmadd-val).
  FATE med samples (avkodarna mot riktiga strömmar) är inte kört.
- **Kompileringstid** (2022 filer, `-j 6`, bäst av 2, delad maskin med
  lastmedel 10; objekt och beroendefiler skrivs till en tom katalog,
  diagnostik skrivs ut):

  | | vägg | mot clang | CPU | mot clang |
  |---|---|---|---|---|
  | clang -O3 | 51,7 s | 1 | 292,8 s | 1 |
  | vpp, direkt, en process per fil | 13,2 s | 3,9× | 71,2 s | 4,1× |
  | vpp, VIR, en process per fil | 14,6 s | 3,5× | 79,8 s | 3,7× |
  | `vpp --batch`, direkt | 0,97 s | 53× | 5,2 s | 57× |
  | `vpp --batch`, VIR | 1,63 s | 32× | 9,1 s | 32× |

  Batchlägets 2022 objekt och beroendefiler är byteidentiska med dem från en
  process per fil (harnessen kontrollerar det), och objekten är desamma som i
  de make-byggda träd som klarade FATE.  Väggtiden varierar med lasten
  (0,85–1,6 s direkt, 1,55–3,4 s VIR i enskilda körningar); CPU-tiden är
  stabil.  Skillnaden mot en process per fil är delade cachar (31 000 av
  38 000 inkluderingar spelas upp ur headercachen, deklarationscachen hoppar
  över 10,9 M token) och ca 35 ms start per process.  Inte mätt: ett bygge
  genom make med vccd, som är den väg en användare får batchlägets cachar på;
  ar och länkning ingår inte.
- **Körtid** (`speed`, `-threads 1`, CPU, en omgång; inte åtgärdat, tas
  senare): totalt VIR 1,70× clang, direkta emittern 2,88×.  Sämst genom VIR:
  mpeg4/mpeg2-kodning 3,15×, convolution 3,4×, gblur 2,7×, aac-kodning 2,3×,
  unsharp 2,3×, ffv1 1,8–1,9×; avkodarna 1,1–1,3×.
