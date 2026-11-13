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

### V4 x86-64 mot clang -O2 (2026-11-12), mål högst 5 % långsammare


2026-11-13, små SQLite-funktioner: byte-sammansättningens strukturella bevis
har flyttats från x86 till `vir/facts.c` (`vir_read_be`). Det normaliserar
konstanta pekaroffsetar och känner även igen två 32-bitshalvor som en 64-bit
big-endian-läsning. x86 väljer en exakt bred laddning och bswap; inga extra
byte läses, och varken volatile, signerade byte eller externt använda
mellanvärden får kombineras. Edits/spillkontrollen ligger kvar i målet.

`vdbeRecordCompareInt`: 102 → 82 maskininstruktioner; åttabytesfallet är
nu movq+bswapq, sexbytesfallet använder movl+bswapl för den osignerade
fyrabytesdelen. Samma regel förbättrar `sqlite3VdbeSerialGet` 138 → 122 och
`walDecodeFrame` 95 → 71. Separata 300-miljonersprov gav cirka 17 % lägre
CPU-tid för sexbytesfallet och 11 % för åttabytesfallet i de länkade
SQLite-objekten. Ett separat fast-alignat funktionsprov ändrade resultatet
kraftigt: vinsterna är **layoutkänsliga**, inte stabila löften per datatyp.

Hela speedtest1, fem alternerade prov, storlek 100: 1275,02 → 1274,71 ms;
upprepning 1274,46 → 1275,21 ms. Båda cirka 1,148× clang, alltså ingen
mätbar helprogramvinst av byteläsningen. Underlag:
`work/vir-x86-perf/byteflags/{results100,repeat100}` och
`work/vir-x86-perf/record-int` (byteflags-serierna innehåller även den
behållna byte64-varianten). Flaggåteranvändning över enkla branchkedjor och
flyttade switchtabeller prövades men behålls inte: omkring 1 % långsammare
SQLite, trots färre instruktioner för flaggvarianten. Målet 1,05× är öppet.

Regressionsprovet täcker 16/32/64 bit, offsetad 32-bit, signerad 48-bit,
alla byte-alignments och separata volatile-läsningar. Egna x86 307/307,
GCC 1615/1615, TCC 105/105, samma fem kända inlinefel; fokuserat ASan/UBSan
och filvist bygge gröna. VIR-formatprov 130/130 och allokatorprov 20/20.
TSVC 151/151 kontrollsummor, 82 vektorloopar och 158 funktioner sänkta.

2026-11-13, allokering vid switch: gemensamma allokatorn flyttar nu levande
värden ur sena/tidiga clobbers även före villkorliga terminatorers utgående
tillstånd. Tidigare hoppades fasen över. x86 kan därför använda r10 mellan
alla switchar och ange det som tidig clobber vid dispatch. Två femprovserier
av speedtest1 storlek 100: 1276,10 → 1268,37 ms och 1273,99 → 1268,01 ms;
senaste då 1,141× clang. Instruktioner/clang 1,364× → 1,358×. Ett syntetiskt
switch-/loopprov fallerar med den gamla allokatorn och passerar med fixen,
även över 48 registerkonfigurationer per funktion och med linjär fallback.
Allokatorprov 20/20 och 30 slumpgenererade enheter gröna.



**2026-11-13: ARM-jämförelse och omprövade x86-försök.** Det aktuella
användarmålet är högst **1,05× clang -O2** för speedtest1; det är fortfarande
öppet. Samma SQLite 3.50.4-källa, `-O2`, `SQLITE_THREADSAFE=0` och
`SQLITE_OMIT_LOAD_EXTENSION` har sänkts med `VCC_VIR=bvls` och allokatorkontroll
för båda målen. ARM använder sin Linux-cross-sysroot. Statiska instruktioner
är inte en jämförelse av cykler mellan olika processorer:

| Funktion | ARM64 instruktioner | x86-64 instruktioner | ARM64/x86 spill |
|---|---:|---:|---:|
| sqlite3VdbeExec | 17083 | 23197 | 379/570 |
| sqlite3BtreeIndexMoveto | 537 | 627 | 0/9 |
| vdbeRecordCompareInt | 81 | 102 | 0/0 |

Konkreta ARM-fördelar: bit-/nollgrenar utan separat flaggmaterialisering,
extenderad indexaddition och skiftad OR i samma instruktion, fler register
och syskonanrop utan call/ret. BtreeIndexMoveto har också en roterad loop
med villkoret i botten. Clangs x86-`-S` bekräftar direkta register-bitprov,
konstantlagringar och vikta minnesoperander som vår x86 ännu ofta missar.

Följande korta, verifierade försök **behålls inte**: färre instruktioner
räckte inte till en upprepad CPU-vinst. Alla tabellprov är fem alternerade
`--size 100 --memdb --testset main`-körningar på CPU 7. Baslinjen är
`9fefc28a` i respektive samma mätserie; medianer i ms:

| Försök | Baslinje | Försök | Utfall |
|---|---:|---:|---|
| register-TEST + adressvikning + fler kopiekällor | 1296,77 | 1288,80 | ej upprepat: 1302,94 mot 1308,31 |
| dessutom omedelbara lagringar | 1302,94 | 1310,20 | instruktioner 1,382× till 1,352× clang, CPU sämre |
| direkta syskonanrop med epilog per plats | 1302,04 | 1325,21 | sämre |
| syskonanrop med delade epiloger för sparade register | 1302,82 | 1318,57 | sämre |
| återanvändning av stacklaster inom block | 1299,70 | 1305,97 | ingen instruktionsvinst i huvudfunktionerna |

Försöken passerade den fokuserade x86-riggen (assembly och egen objektväg)
och SQLite-verifieringshashen; syskonanrop kontrollerades dessutom med
normal retur i samma funktion, heltals-/FP-retur, registertryck, stackargument
och en lokal adress som måste leva genom anropet. Nollidiomer/smalare
konstantkodning gav inte heller säker CPU-vinst och är återställda.
Den behållna baslinjen ligger i dessa serier ungefär **1,15–1,16× clang**;
den tidigare storlek-100-serien gav 1,143×. Ingen ny prestandavinst eller
uppnådd grind hävdas. Underlag och assembly finns i
`work/vir-x86-perf/{arm-compare,testi2,testaddr,framecopies,sti,sib2,sibshared,reload}`.


**2026-11-13: dela optimeringsunderlaget mellan målen.** Historiken visar att
flera generella delar ligger i ARM-sänkningen och därför riskerar att portas
som en andra implementation. Första uttaget är nu `vir/facts.c`, utan beroende
på parsern, emitteraren eller CPU-register:

- `vir_low32`: ARM:s bakåtriktade fixpunkt för vilka användningar som bara
  behöver de låga 32 bitarna, inklusive loopparametrar, select, anrop och
  retur-ABI. ARM anropar den gemensamma implementationen.
- `vir_mask32`: x86:s begränsade analys av möjliga ettbitar i ett i32-uttryck.
  x86 använder den gemensamma funktionen i sina befintliga extensionsbevis.
- `virtest --selftest` kontrollerar maskfakta och efterfrågan genom en loop
  för både full och smal retur. `vir_low32.c` prövar loopar, select och smala/
  fulla anropsargument på båda CPU:erna.

Det är ett uttag av befintlig logik: ARM:s SQLite-assembly och x86:s
SQLite-objekt är byteidentiska med före ändringen. Ett separat försök att
använda low32-fakta för fler borttagna x86-extensions gav färre instruktioner
men cirka 1 % sämre CPU-tid i upprepade korta speedtest1-prov; det aktiveras
inte som del av uttaget.

| Historik | Redan gemensamt / vad som bör delas härnäst |
|---|---|
| `ef98fd06`, PR126 | Allokatorns loop-entry-, argumentflytts- och callee-save-regler ligger redan i `vir/alloc.c`; ingen ny x86-port behövs. |
| `6d7fc7f8`, `7d51e03c` | Inlining och dominanta återanvända laster ligger i den gemensamma byggaren; x86 använder dem redan. |
| `f1bc6bc1`, `eaa9b8f8` | Nu gemensamt i `vir/layout.c`, inklusive kopian av inträdestestet. Båda emitterarna använder planen. |
| `25157280`, `5f05a106` | Beviset att ramen inte undkommer och att ett resultat bara vidarebefordras till retur är IR-gemensamt. ABI, registerflyttar och epilog/jump hör till målet. |
| `0c2ec7ed` | Matchning av fältadresser, skala och extension kan ge en gemensam adressform. ARM:s tillåtna extend/scale och x86:s SIB-regler avgörs därefter per mål. |
| `1795593f` | Save-point-analysen finns i allokatorn; målens val av instruktioner som behöver ram samt prolog/epilog-emission återstår per CPU. |

Validerat: `vir-test` 130/130, nya analysprovet i `virtest --selftest`,
fokuserad x86-körning via assembly och egen objektväg, ASan/UBSan samt
filvis bygge. Det nya C-provet går på ARM64/QEMU; bytejämförelserna ovan
kontrollerar båda målens SQLite-output. Ingen runtime-vinst hävdas för
refaktoreringen.

**Gemensam layout inkopplad (2026-11-13).** `vir/layout.c` äger nu
chasing av tomma grenar (med bevarade allocator-edits/edge moves), loopordning,
rotationsplan och beslut om kopior av inträdestest/små joins. Både ARM och x86
anropar samma implementation. Emitterarna sköter fortfarande sina register,
instruktioner, kantrörelser och ramgränser. Ingen kopia av ARM:s planerare i
x86-filen. `VCC_VIR_ROT=0` stänger nu av rotation även på x86.

ARM:s SQLite-assembly är byteidentisk. x86 använder faktiskt rotation med
inträdestestets kopia, inte bara flyttad blockordning. Fem alternerade korta
speedtest1-prov, storlek 100 på CPU 7:

| Serie | clang CPU ms | Före CPU ms | Delad layout CPU ms | Delad layout/clang |
|---|---:|---:|---:|---:|
| Första | 1116,16 | 1283,71 | 1271,89 | 1,140× |
| Upprepning | 1112,49 | 1284,06 | 1276,63 | 1,148× |

Instruktioner/clang: 1,382× → 1,364×. Förbättringen mot föregående x86 är
cirka 0,6–0,9 % CPU, men **målet 1,05× är fortfarande öppet**. Kostnaden är
mer kod: sqlite3VdbeExec 107831 → 116982 byte. En kort byggmätning för
sqlite3.c via -S är cirka 0,83 → 0,88 s. Underlag:
`work/vir-x86-perf/shared-layout/{results100,repeat100}`.

Validerat: egna x86 307/307, GCC 1615/1615, TCC 105/105; inline 83/88 med
samma fem kända fel. TSVC 151/151 kontrollsummor, 82 verkliga vektorloopar,
158 funktioner sänkta. `virtest --selftest` prövar ordning, guardkopior,
adresstagna block, självcykel, kantrörelser och edits som förbjuder chasing.
VIR-formatprov 130/130. Befintliga ARM-prov `vir_loop_rotate.c` och
`vir_join_dup.c` ingår även i den fokuserade x86-riggen (assembly, egen
objektväg, ett/sex emittertrådar). ASan/UBSan och filvist bygge är gröna.

Ett efterföljande x86-försök att undvika returblockskopior och vika tomma
returblock direkt till den gemensamma epilogen behålls inte: första serien
1285,08 → 1274,34 ms upprepades inte (1275,09 → 1275,76 ms). Underlag i
`work/vir-x86-perf/returnfold`; patch sparad utanför checkouten. Det delade
layoutsteget ovan är den behållna versionen.

Nästa port av syskonanrop ska på samma sätt börja med gemensamma IR-bevis
för frame escape och returvidarebefordran, med målspecifik epilog/emission.

Efter rebase på main `06c688f4` finns PR126:s gemensamma allokatorregler
med. Första x86-skivan: sekventiellt konsistenta atomiska laster/lagringar,
heltals-mulhi och switch (täta relativa tabeller och glesa intervall) öppnar
bl.a. sqlite3VdbeExec för VIR. Omedelbara operander och skift behöver inget
register; ett konstant skift väljs från IR även när konstanten redan har
ett register. Jämförelse direkt följd av branch återanvänder flaggorna och
kan slopa boolmaterialiseringen. Villkorskanternas registerkopior placeras
utanför huvudflödet; kanter utan kopior går direkt till målblocken. ADD
använder LEA:s tre operander; en ptradd med konstant offset och en enda
omedelbar minneskonsument viks när inga allokator-edits kommer emellan.

**Snabb, kontrollerad mätning:** `bench/sqlite/vir-x86.py build --source DIR`
kör VIR via -S + clangs assembler (0,63 s för sqlite3.c i stället för den
långsamma textassemblerns objektväg). `run clang=EXE baseline=EXE vir=EXE`
kör tre alternerade perf-stat-prov av `--size 30 --memdb --testset main`,
varje körning under 0,6 s. Alla exits och verifieringshashar kontrolleras;
råutdata, binärhashar och medianer sparas i summary.json. Den gamla riggens
tidtagning nådde ett saknat R-tree-test och ignorerade exitstatus; run.py
kräver nu main och lyckad exit. De äldre väggtiderna är inte en slutgrind.

| Version | CPU-tid median | Cykler/clang | Instruktioner/clang |
|---|---:|---:|---:|
| clang -O2 | 279,05 ms | 1,000 | 1,000 |
| x86 före denna skiva | 529,27 ms | 1,916 | 2,560 |
| efter, inklusive PR126 | 386,78 ms | 1,399 | 1,626 |

Verifieringshash samma. **10 %-målet är fortfarande öppet.** Nästa mätpunkt
är indexerade minnesoperander: fler separata adressberäkningar kan tas bort.
GCC 1615/1615, TCC 105/105, egna x86 297/297; inline 83/88 med samma fem
kända fel. Riktad rigg kräver riktig VIR-sänkning för switch/intervall,
atomics och signerad/osignerad mulhi; både -S och egna objekt körs.
ASan/UBSan i den riktade riggen är grön. TSVC behåller 151/151 och 82
verkliga vektorloopar; nu går alla 158 byggda funktioner genom VIR.

Nästa skiva viker ptradd och intilliggande i64-skala till x86:s indexerade
minnesoperand (1/2/4/8). Enbart engångsvärden utan spill viks; allokatorns
edits får inte skriva över det bevarade indexet. Effekter och kopiepass känner
bas och index. Riktade körprov täcker negativa index, signerade små laster
och volatila lagringar, med krav på faktisk indexerad maskinkod.
Fem korta alternerade main-prov: föregående CPU 393,79 ms (1,380x),
indexerad 393,38 ms (1,378x); instruktioner 1,626x → 1,603x. Ingen säker
CPU-vinst; 10 %-målet är öppet. GCC/TCC/egna fortsatt gröna, samma fem
inline-fel. Mätdata: work/vir-x86-perf/indexed/recheck/summary.json.

Tredje skivan låter true-kanten falla igenom när dess block står näst,
slopar identitetskopior vid cast och ger destruktiva operationer en tied-
preferens i allokatorn. i32 lovar inga övre registerbitar; en senare EXT
måste fortfarande utvidga. x86 använder nu också den gemensamma VIR-
inlinerns storleksanalys och engångsanrop (tidigare uttryckligen avstängd
för x86 i tu.c). Ett större engångshjälpanrop måste försvinna i riktprovet.
Fem alternerade prov, samma CPU: clang 281,65 ms, föregående 383,12 ms
(1,360x), ny 356,79 ms (1,267x); instruktioner 1,603x → 1,505x.
Verifieringshashar lika. En separat provad looplayout gav ingen mätbar vinst
och behålls inte. GCC 1615/1615, TCC 105/105, egna 299/299, samma fem
inline-fel; riktade objekt-/assemblyprov och ASan/UBSan gröna. 10 %-målet
är fortsatt öppet. Data: work/vir-x86-perf/inline-final/results/summary.json.

Fjärde skivan känner igen två intilliggande osignerade byte-laster som
bildar ett big-endian-ord. Fem sammanhängande instruktioner med engångsvärden,
inga spill och inga mellanliggande edits blir en exakt 16-bitarslast och
byte-rotation. Volatila/signerade läsningar och mellanliggande lagringar
kombineras inte. Fem kortprov: CPU 361,49 → 355,25 ms (1,283x → 1,261x
clang), instruktioner 1,505x → 1,491x. GCC/TCC fortsatt 1615/105 gröna,
egna 300/300, samma fem inline-fel, riktprov och ASan/UBSan gröna. TSVC
efter inlinerskivan: 151/151, 82 vektorloopar, 158/158 funktioner sänkta.
Data: work/vir-x86-perf/be/results/summary.json; 10 %-målet öppet.

Femte skivan använder CMOV för skalära heltals-/pekarval och samma säkra
flaggåteranvändning som branch. Båda källorna överlever resultatöverlapp;
en jämförelse som har fler användare behåller sin boolmaterialisering.
Riktprov täcker signerad/osignerad jämförelse, boolvillkor och pekare, med
krav på CMOV i verklig kod. GCC/TCC 1615/105, egna 301/301, samma fem
inline-fel; assembly/objekt och ASan/UBSan gröna. SQLite instruktioner
1,491x → 1,482x, CPU oförändrad (360,55 → 360,41 ms; clang 284,89 ms).
Ingen belagd CPU-vinst från denna skiva. Data: work/vir-x86-perf/cmov2/results.

Sjätte skivan viker även add.i64 + i2p till minnesoperand och känner igen
exakt två/fyra vanliga byte-laster i big-endian-ord. Fyra bytes blir en
32-bitarslast och bswap; samma regler om engångsvärden, spill och edits
gäller hela sekvensen. Ett indexerat byte-ord kan behålla bas/index genom
de borttagna instruktionerna; ingen överläsning eller ändring av volatila
accesser. Switch-tabellen använder också indexerad last direkt.
Fem kortprov: CPU 351,27 → 348,80 ms (1,251x → 1,242x clang), instruktioner
1,482x → 1,461x. Samma GCC/TCC/egna 1615/105/301 och fem inline-undantag;
utökade negativa index/32-bitarsord, assembly/objekt och ASan/UBSan gröna.
Arithmetik direkt från spillslot och dubblerad inlinegräns gav ingen säker
vinst och behölls inte. Data: work/vir-x86-perf/be4/results/summary.json.

Sjunde skivan ger identitetskonverteringar tied-preferens, känner igen
när en i32-definition redan nollställer övre registerbitar och använder en
begränsad bitmaskanalys för icke-negativa signerade utvidgningar. Parametrar,
anropsresultat och identitetstrunkeringar antas inte ha rena övre bitar.
Signerade laster är redan 64-bitars utvidgade, men en spillslot kan bryta
detta: endast värden utan spillslot får återanvända signutvidgningen.
Riktprov för smutsiga SysV-register/returer, trunkering, masker och negativa
bytevärden går via assembly/objekt och ASan/UBSan. GCC/TCC 1615/105, egna
302/302, samma fem inline-undantag. Fem kortprov: instruktioner 1,461x →
1,439x clang, CPU 348,58 → 348,45 ms (1,245x → 1,244x): ingen säker CPU-vinst.
Data: work/vir-x86-perf/ext2/results. Standardstorlek 100 före denna skiva:
tre prov ger 1,249x CPU; det korta storlek-30-provet är representativt.

Åttonde skivan sänker konstanta små memcpy (högst 128 byte) och nollställning
(högst 256 byte) utan REP:s fasta register och clobbers: SSE2 för hela
16-bytebitar, skalära 8/4/2/1-byte-svansar utan överläsning. Större,
dynamiska, volatila och icke-nollfyllningar behåller tidigare väg.
Fem kortprov: CPU 347,99 → 344,40 ms (1,233x → 1,220x clang), instruktioner
oförändrade kring 1,439x. Riktprov täcker gränsstorlekar, ojusterade adresser,
svansar, 256-byte-lokalnollning och reservvägar. GCC/TCC 1615/105, egna
303/303, samma fem inline-undantag; assembly/objekt och ASan/UBSan gröna.
Data: work/vir-x86-perf/block/results/summary.json; 10 %-målet öppet.

Nionde skivan viker en vanlig osignerad last, konstant AND och eq/ne mot
noll till TEST direkt på minnet, med exakt ursprunglig åtkomstbredd. De två
slopade värdena får bara ha en användare och inga spill/edits. Jämförelsens
boolvärde materialiseras när det behövs; efterföljande branch/CMOV kan
återanvända flaggorna. Volatila/signerade eller delade värden kombineras inte.
Riktprov täcker byte/ord/dubbelord/64 bitar, index och fältdisplacement,
signutvidgade masker, delad bool och reservfallen. GCC/TCC 1615/105, egna
304/304, samma fem inline-undantag; assembly/objekt och ASan/UBSan gröna.
Fem kortprov: CPU 345,64 → 343,66 ms (1,223x → 1,216x clang), instruktioner
1,439x → 1,433x. Data: work/vir-x86-perf/testm2/results/summary.json.

Tionde skivan använder en virtuell rambas när -fomit-frame-pointer gäller.
Sista utskriftspasset räknar om den till rsp, inklusive tillfälliga utgående
stackargument. rbp blir en sjätte allokerbar callee-save. Prolog/epilog och
CFI följer rsp; vanliga pekarvärden i rbp skiljs från rambasen. Lokal- och
spillslots har samma konceptuella offset som tidigare; -fno-omit-frame-pointer
behåller äldre väg. Fem kortprov: CPU 347,82 → 333,56 ms (1,240x → 1,189x
clang), instruktioner 1,433x → 1,389x. GCC/TCC 1615/105, egna 305/305,
samma fem inline-undantag; riktad backtrace genom stackargument, smutsigt
bevarat rbp och båda ramvalen, assembly/objekt samt ASan/UBSan gröna.
Unity och fil-för-fil bygger och kör riktproven. TSVC: 151/151, 82 verkliga
vektorloopar, 158/158 funktioner sänkta. Data: work/vir-x86-perf/spframe/results.
10 %-målet är öppet.

Elfte skivan kan allokera r10 och använda r11 som generell sänkningsscratch.
Anrop deklarerar då r10 som caller-clobber. Tät switch får en fast, död
selector i r10; glesa/delade switchvärden behåller tidigare reserv. Om
allokatorns kantkopior behöver sin GP-scratch samtidigt räknas funktionen
om med två reserverade GP-register. Skalär fabs/fneg bygger nu SSE2-masker
utan GP-scratch. Ett rotationsprov med fler levande värden än register och
bitexakta noll-/NaN-/infinitetstecken täcker båda FP-bredderna.
Fem kortprov: CPU 331,41 → 325,35 ms (1,182x → 1,161x clang); instruktioner
1,389x → 1,388x. GCC/TCC 1615/105, egna 306/306 och samma fem inline-fel;
assembly/objekt och ASan/UBSan gröna. TSVC 151/151, 82 vektorloopar och
158/158 funktioner sänkta. Data: work/vir-x86-perf/r10/results/summary.json.


### V2/V3 x86-64 (2026-11-12): TSVC och fem verkliga korpusar

Efter första bladskivan provades TSVC_2, SQLite 3.50.4, PostgreSQL 18.6
(inklusive psql), CPython 3.13.2 och zlib 1.4 på Linux x86-64. Isolerade
byggträd och alla rådiagnoser ligger under `work/vir-x86-corpus/`; versioner,
commit-id och kompilatorns hash i `manifest.json`. `bench/vcc-vir-corpus.py`
är en VPP-wrapper som tvingar x86-64/-O2/-g0, kör verifierare och
allokerarkontroll och sparar kommando, arbetskatalog, tidsåtgång, fel och
VIR-täckning per kompilering. `--summary DIR` skriver `summary.json`;
förväntade configure-prober hålls skilda från riktiga TU-fel. Använd den som
`VPP=$PWD/bench/vcc-vir-corpus.py VCC_VIR_CORPUS_LOG=DIR` med projektens
befintliga byggriggar. Systeminclude-sökvägar kan anges med
`VCC_VIR_CORPUS_SYSINCLUDES` (annars upptäcks LLVM-/Linux-sökvägarna).

**Ändrat efter fynden:** riktiga SysV-anrop (direkta, indirekta och
variadiska anrop; separata heltals-/SSE-registerbanker och stackargument,
upp till 23 argument), anropsklobber och FP-spill; registersave-områden för
variadiska *definitioner* saknas fortfarande. `returns_twice` och setjmp-
namnen går den tidigare vägen. FCMP följer NaN-reglerna; scalar select,
heltals-/FP-konverteringar, fneg/fabs/fsqrt, packad jämförelse/maskval,
i32x4-multiplikation på SSE2 (`pmuludq` + ompackning), i32x4→f32x4 och
ordnad banvis i64x2→f64x2 är sänkta. Memcpy/memset använder rep movsb/stosb
med fasta register. Ostödda operationer namnges nu i reträttens orsak.

| Korpus | Resultat med den nya sänkningen | Sänkta/byggda VIR-funktioner |
|---|---|---|
| TSVC_2, pinnen i fetch-testsuites.sh, 256 iterationer | 151/151 checksummor mot clang; 82/82 vektorkandidater når maskinkod | 157/158 (s442 har switch) |
| SQLite 3.50.4 | Testfixture och shell byggda; nya sqlite3.c + speedtest1 körs, verifieringshash samma som clang | 1737/1883 i speedtest-bygget |
| zlib 1.4 | 15/15 biblioteks-TU:er; example/infcover, minigzip-rundresor och samma storlekar/CRC som clang | 147/154 |
| PostgreSQL 18.6 | 1504 C-kompileringar, 13 avvisade på intrinsicluckor; strikt vcc-bygge är alltså inte grönt | 24411/26439 |
| CPython 3.13.2 | 334 C-kompileringar, fyra avvisade; också separata länkluckor i mimalloc | 12239/13044 |

Antalen är instrumenterade funktionsinstanser, inte procent av alla
källfunktioner (byggaren kan också avstå). Ingen verifierar- eller
allokerarkontroll rapporterar fel. PostgreSQL länkas separat med clang
endast för de 13 avvisade kompileringarna: psql/postgres --version fungerar,
meson ger **81 godkända, 0 fel, 249 överhoppade**. Det är uttryckligen ett
blandbygge, inte godkänd vcc-korpus eller stängd G2/G4-grind. CPython kan
inte länkas ens efter motsvarande fyra clang-kompileringar; se TODO T13.

TSVC:s ursprungliga fem sänkta funktioner blev först 117 med anropen
(41 verkliga vektorloopar), sedan 157 (alla 43). Riggen skiljer nu mellan
vektoriserarens *kandidater* och loopar i faktiskt sänkta funktioner; den
förra rapporten kunde räkna en vektorloop vars hela funktion föll tillbaka.
**Nästa vektorskiva når 82 av 125 granskade loopar, alla i sänkta
funktioner; 151/151 kontrollsummor.** Från 43 verkliga vektorloopar före
ändringen: stridade laster/lagringar med banvis gather/scatter, indexerade
laster endast när skrivna objekt bevisligen är andra symboler, första-
ordningens rekurrenser (även två fördröjda värden), ordnade flyttalssummor,
kedjade reduktioner och kopplad max/index-reduktion. Nya `vinsert`/`vextract`
är typade, verifierar banindex och sänks med SSE2/NEON. Flyttalsreduktionen
extraherar banor och räknar i skalär ordning: ingen reassociering eller
fast-math. Beroendekontrollen skiljer stridens restklasser och räknar hela
adressintervallet inklusive mellanrum vid runtime-aliasprovet.

If-konverteringen återanvänder exakta uttryck och redan lästa värden över
villkorsarmen endast utan mellanliggande skrivning/anrop; flera phi-värden
blir select tillsammans. Ingen ny spekulativ minnesläsning. Skalär x86-
division/rest är också sänkt med RAX/RDX-klobber och separat divisor.
TSVC:s summerade looptider på delad värd: före direkt 3,87 s, VIR 2,36 s,
clang -O3 0,84 s; efter direkt 3,84 s, VIR 1,67 s, clang 0,88 s. En kort
körning vardera, ingen kvalitetsgrind. Återstår framför allt riktiga
minnesberoenden och villkorliga lagringar, inte uppluckrade beroendekrav.
SQLite speedtest1 storlek 10: VIR 0,25 s mot clang 0,13 s, samma hash;
switch-reträtten omfattar ännu de stora dispatchfunktionerna. Zlib, best
av en CPU-körning: direkt 610 ms, VIR 626 ms, clang 359 ms; korrektheten är
god, generell kodkvalitet återstår.

Nya runtime-regressioner testar stack-/indirekta/variadiska anrop och
levande värden över dem, NaN/inf/signed-zero i scalar/packad jämförelse,
maskval, modulo-multiplikation och 32-/64-bitars vektorkonvertering. Riggen
kräver att dessa funktioner verkligen sänks och att SIMD-instruktionerna
finns, med vektorisering av/på, ett/sex emittertrådar och både clangs och
vår egen objektväg. Unity/nonunity, ASan/UBSan och TSan är gröna i den fokuserade riggen. Egna
x86-runtimeprov **285/285**, GCC **1615/1615**, TCC **105/105**; inline
**83/88**, samma fem kända fel. De fokuserade C-proven går också på
arm64/QEMU. Hela TSVC där: **151/151**, **77** verkliga vektorloopar
(målens byggarformer skiljer sig; x86-64 ger 82). VIR-formatproven omfattar
nu insert/extract och avvisar fel banindex och bantyp; 300 ASan-mutanter
överlever utan krasch. Sanitizern hittade dessutom signed negation av INT64_MIN i
assemblerns uttryck: negationen är nu unsigned modularitet, nytt objektprov;
vas-obj **61/61 byteidentiska**, även ASan/UBSan.

Slutlig korpusomkörning: zlib byggs om genom VIR och klarar självtest,
storlekar och CRC. SQLite blottlade ett dött skalärt resultat i pageFreeArray:
x86-sänkningen använder nu reserverat scratchregister när allokatorn anger
VR_NONE, så varken ogiltig registertext eller krasch i -S kan uppstå.
SQLite speedtest1:s verifieringshash är åter samma som clangs (0,26 mot
0,14 s i storlek 10). Arm64:s befintliga switch-resultattabellprov hittade
att sänkningen kastade byggarens tabellkö före end_function; kön bevaras nu
till emission, och de sju riktade switch-proven går igenom. Hela arm64:s
egna runtime-svit: **290/290**, ett känt xfail.

Nästa backendsteg: switch, mulhi och fler ABI-grupper, följt av
SIMD-poster/adressvikning och fler loopformer. De hittade intrinsic-/inline-
luckorna är egna frontend-/ABI-punkter, inte skäl att dölja fallback i
projektbyggena.


### V2/V4 x86-64 (2026-11-12): första allokerade bladfunktioner och SSE2

`VCC_VIR=bl` på x86-64 går nu genom samma byggare och loopvektoriserare som
arm64; funktionen märks `x86_64-linux`. `arch/x64/emit/vir.c` sänker skalära
bladfunktioner med SysV:s heltals-/FP-register och stackparametrar, fasta
`cl`, callee-sparade register, spill och sekvenserade kantflyttar. Ram- och
spilltabeller återanvänds per `ecx`. `VCC_VIR_CHECK=1` kontrollerar den riktiga
målallokeringen. Lägescacherna för VIR, vektorisering och jämförelsesammanslagning är trådlokala;
sextrådsprovet hittade och avlägsnade den sistas kapplöpning.

SSE2-skivan omfattar 128-bitars laddningar/lagringar, float/double-aritmetik,
heltalsaddition/subtraktion och bitoperationer, splat, indexföljd, omvända
banor och heltalsreduktion. Den delade vektoriseraren behåller sina alias-
och restloopskontroller. En räknare i `cl` måste överleva även när resultatet
får `rcx`; regressionen testar detta särskilt. SIMD skrivs tills vidare som
opaka `X_RAW`-poster: korrekt men konservativt för postoptimeraren.

`bench/vcc-vir-x86.py` kräver nio faktiskt sänkta funktioner och fem loopar
med SSE2-instruktioner; de körs både med och utan vektorisering, via clangs
assembler och vår egen objektväg. Provet jämför en och sex emittertrådar och
kräver identisk `-O0/-O1`-text med VIR av/på. Unity, separatfilbygge och
ASan/UBSan går grönt (också hela egna x86-sviten: 282/282). Linux x86-körsviter utan cache med VIR + allokerarkontroll:
**1615/1615 GCC, 105/105 TCC, 282/282 egna**. Inline-sviten är **83/88**:
samma fem fel och 22 undantag med VIR av som på; inga nya fel.
Det nya egna provet kör också på arm64/QEMU. De 22 egna `vir_`-proven
går även med `-fPIC`. Det riktade provet är grönt under TSan med sex
emittertrådar och körs i det befintliga x86-värdade TSan-CI-jobbet.

Kort mikromätning på denna delade x86-värd: 4096 float-element,
`a[i]=b[i]*c[i]+1.5f`, 10000 anrop, strikt `-ffp-contract=off`, median av sju
växelvisa körningar (CPU-tid): direkt **26,931 ms**, VIR **7,709 ms**,
clang -O2 **2,427 ms**, samma kontrollsumma. VIR är 3,49 gånger snabbare än
direktvägen men fortfarande 3,18 gånger långsammare än clang på just detta
prov; ingen allmän kvalitetsgrind är uppnådd. Varm processkompilering av den
lilla TU:n, median av elva: direkt **2,86 ms**, VIR **3,27 ms**, clang
**18,12 ms** (processstart ingår). Källor, assembler och alla sampel finns i
`build/linux/vir-x86-results/measurements.json` och dess katalog.

**Kvar för V2/V3/V4:** anrop, fler ABI-grupper, varargs, division, fler
skaläroperationer, vektormultiplikation/-jämförelser/-konverteringar och
maskval. Ostödda funktioner använder den tidigare vägen. VIR är fortsatt
uttryckligen påslaget och bara för `-O2`; varken x86-täckningsgrinden eller
V4:s kodkvalitetsgrind är stängd. Nästa kodkvalitetssteg: egna SIMD-poster,
konstanta skift utan `cl`, adressvikning och effektivare loopgrenar.


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

### Vad allokerarens kvalitet är värd: LLVM:s allokerare mot varandra (2026-11-12)

Mätningen som "Mäts i V4" nedan utlovade.  sqlite3.c, speedtest1 `--size 100
--memdb`, Homebrew clang 23.1.4 `-O2` med `-mllvm -regalloc=X`, vcc på
75bb9062 (`VCC_VIR=bl`), virpol är binären från 12:36 samma dag (äldre
inlining).  Fem växelvisa rundor, `/usr/bin/time -l`; maskinen hade last 10,
så cyklerna är median av parade kvoter med [min .. max], och bara skillnader
över ca 2 % säger något.  Samma verifieringshash i alla sju.

| | Instr. | Cykler mot greedy | Statiskt: totalt | `mov` r,r | sp/fp-laster | sp/fp-lagringar |
|---|---|---|---|---|---|---|
| clang greedy | 23,70 G | 0 | 239 k | 21,3 k | 13,0 k | 10,2 k |
| clang pbqp | +3,5 % | +2,1 % [−0,6 .. +10,7] | 242 k | 22,7 k | 14,7 k | 10,4 k |
| clang basic | +3,7 % | +7,0 % [+4,7 .. +19,3] | 242 k | 17,4 k | 19,6 k | 10,7 k |
| clang fast | +44,1 % | +72,4 % | 325 k | 3,1 k | 83,3 k | 44,8 k |
| virpol | +3,4 % | +6,0 % [+2,2 .. +14,1] (bäst av fem +3,9 %) | 242 k | 21,1 k | 12,6 k | 10,0 k |
| VIR | +15,3 % | +12,6 % [+10,8 .. +23,9] | 298 k | 35,1 k | 22,0 k | 17,9 k |
| vcc utan VIR | +60,9 % | +50,7 % | 234 k | 24,0 k | 28,5 k | 21,9 k |

- **VIR mot virpol: +8,4 % cykler [+6,3 .. +8,6], +11,5 % instruktioner.**
  VIR mot clang basic: +4,1 % [+3,7 .. +7,4].
- **Greedy mot basic är värt ca 7 % här**, lika mycket som hela VIR:s gap mot
  virpol.  Basic har färre flyttar än greedy (17,4 k mot 21,3 k) men 50 % fler
  stacklaster: det som kostar är omladdningar på heta vägar (delning av
  livslängder), inte antalet `mov`.  Fast visar golvet utan allokering.
- **Statiskt överskott VIR mot virpol, 56,5 k instruktioner:** flyttar 14,0 k,
  stacklaster 9,3 k, stacklagringar 8,0 k (tillsammans 55 %: allokering och
  ramar), hopp 4,9 k, konstanter 3,3 k, övrigt 17 k (urval, kopierade block).
- **Inte mätt:** allokerarens andel av de 8,4 procenten direkt (kräver samma
  funktioner genom båda allokerarna; receptet för virpol finns inte i
  trädet), och inget dynamiskt: de statiska talen säger inte vilka flyttar och
  laster som körs.  Uppskattning: virpol med basic skulle ligga på ungefär
  VIR:s nivå (8,11 G × 1,07–1,08 mot 8,79 G).
- **Slutsats för arbetsordningen:** omladdningar i loopar före flyttar.
  PBQP:s +2 % visar att en allokerare utan delning kan komma nära greedy med
  en bra kostnadsmodell.

### Allokeraren: loopingång, argument som parallell flytt, callee-saved direkt (2026-11-12)

Första steget efter mätningen ovan, efter sammanfogning med main (d0cbdcc7).
Mätt i **instruktioner** (`/usr/bin/time -l`, speedtest1 `--size 100 --memdb`):
de är stabila till ca 0,05 % också med last 10–20 på maskinen, vilket cyklerna
inte är.  Bas efter sammanfogningen: 26,18 G (virpol 24,49 G, clang 23,70 G),
8,5–8,6 G cykler (virpol 8,24 G, clang 7,86 G).

- **Vad som faktiskt körs** (`VCC_VL_PROBE=MASK` i `emit/vir/a64.c`: en `nop`
  efter varje redigering av ett slag; skillnaden i instruktioner är antalet
  körningar, en övre gräns eftersom `nop` också stoppar postvikningarna):

  | Slag | Före instruktion | På kant | Summa | Andel av 26,1 G |
  |---|---|---|---|---|
  | flytt | 1,23 G | 0,60 G | 1,83 G | 7,0 % |
  | rematerialisering | 1,00 G | 0,55 G | 1,55 G | 5,9 % |
  | omladdning | 0,34 G | 0,28 G | 0,62 G | 2,4 % |
  | lagring till plats | 0,16 G | 0,34 G | 0,50 G | 1,9 % |

  Flyttar och konstanter körs alltså tre gånger så ofta som omladdningar och
  lagringar.  Slutsatsen i förra avsnittet ("omladdningar i loopar före
  flyttar") gällde LLVM:s basic mot greedy och gäller inte vår allokerare på
  den här lasten.  Flyttarna före instruktion per orsak: kopia av ett levande
  värde till ett anrops argumentregister 0,73 G, parameter eller resultat till
  callee-saved 0,29 G, levande värde undanflyttat ur ett argumentregister
  före ett anrop 0,30 G, övrigt 0,15 G.  De två första följer av
  anropskonventionen; clang har dem också.
- **Gjort, tre regler i `vir/alloc.c` (SPEC 15.6–15.7 uppdaterad):**
  1. *Loopingång* (`preload`): ett värde som når loophuvudet i sin plats och
     används i loopen får register i huvudets ingångstillstånd, så att kanten
     in i loopen laddar det en gång.  Bara medan register finns över
     (maxp + 1); i en loop med anrop bara callee-saved.  Per loop finns nu
     `lcall` (ett anrop utanför kalla block).
  2. *Fasta operander som parallell flytt:* ett argument som flyttar från ett
     argumentregister till nästa kopieras innan dess register skrivs.  Det
     reducerade fallet (`add4(v, 96, base, n, 0, zAff, n)`): 8 → 6
     instruktioner.
  3. *Callee-saved direkt:* ett värde som lever över ett anrop tar ett ledigt
     callee-saved register också utanför loopar och när funktionen inte använt
     något än.  Förut hamnade det i ett caller-saved och flyttades vid anropet.
- **Resultat, sqlite:**

  | | Instr. | Statiskt totalt | `mov` r,r | sp/fp-laster | sp/fp-lagringar |
  |---|---|---|---|---|---|
  | bas | 26,186 G | 289 168 | 34 535 | 22 379 | 18 100 |
  | 1 | −0,18 % | 289 060 | 34 486 | 22 318 | 18 111 |
  | 1 + 2 | −0,23 % | 288 572 | 34 057 | 22 260 | 18 110 |
  | 1 + 2 + 3 | **−0,42 %** (26,076 G) | 285 866 | 31 591 | 22 269 | 17 996 |

  Cykler: fyra parade körningar inom bruset (last 23).  Ramar oförändrat 1138.
- **Prövat och struket, alla mätta i instruktioner:**
  - *Värde ur ingångstillståndet vid ett sammanflöde* när en annan föregångare
    har det i plats och blocket inte läser det: statiskt −3,4 k stacklaster
    (−15 %), dynamiskt **+0,6 %**.  Kantomladdningarna (8,6 k statiskt, varav
    7,0 k för värden som målblocket inte läser) ligger alltså mest på kalla
    vägar; de statiska talen i förra avsnittet överskattar dem.  Den säkra
    varianten (blocket anropar före användningen) gav −264 laster och inget
    mätbart.
  - *Konstanter i register över loopar utan anrop:* +838 instruktioner
    statiskt, +0,07 % dynamiskt (fler rematerialiseringar på kanterna).
  - Sampling (`sample`) per instruktionsslag: samplen hamnar efter
    felförutsagda hopp och säger inget om instruktionsblandningen.
- **Tester:** `vir-alloc` 19/19 (nya `loop-preload.vir`, `args-parallel.vir`;
  `irr-head.vir` och `irreducible.vir` ombekräftade: omladdningen flyttad till
  kanten), slump 600 enheter (frö 1–300, 1000–1299), `mutate` 3243 av 3355;
  `vir-test` 126/126; C-sviterna 2091 utan VIR (vcc 288/288), genom VIR med
  kontrollen 2088/2091 (de tre kända: jump_table, switch_dispatch,
  switch_values); inline genom VIR 85/85; nytt `vcc/vir_loop_entry.c`; sqlite
  genom VIR med kontrollen, hash oförändrad.  Inte körda: `vbench.py gate`,
  TCL-sviten, PostgreSQL, zlib.
- **Kvar, i ordning efter vad proben visar:** flyttar på kanter (0,60 G:
  blockparametrar och bakkanter), undanflyttningen före anrop (0,30 G, ett
  värde som inte är `xcall` enligt levande mängderna men sitter i ett
  argumentregister), rematerialisering på kanter (0,55 G: konstanter som
  blockargument).  Gapet mot virpol i instruktioner (+6,5 %) är större än allt
  allokeraren kan ta av detta; loopar som inte roteras (ett `b` per varv) hör
  till sänkningen.

### Ram i funktioner med hopptabell, och var kantflyttarna körs (2026-11-12)

- **Mot clang -O2 efter förra avsnittet** (sex parade körningar, last 11–14):
  cykler **1,095x** [1,083 .. 1,125], instruktioner 1,100x; virpol mot clang
  1,049x [1,034 .. 1,086].
- **Ram:** `opt_regs` (arm64, `opt.c`) gav `OPF_ALL` för `I_JT`, så en
  funktion med hopptabell behöll ramrekord och alla callee-saved fast den inte
  behövde dem.  `I_JT` läser sitt index och skriver x16/x17; städpassen ser den
  fortfarande som `OP_ALL`.  vdbeRecordCompareInt (bara syskonanrop) och två
  till i sqlite tappar `stp/mov/ldp`: 26,10 G → 25,97 G instruktioner
  (**−0,5 %**, mer än de tre allokeringsreglerna tillsammans), statiskt 1138 →
  1135 ramar.  Gäller också den direkta emittern.  Test:
  `vcc/vir_switch_leaf.c`; vcc-cfi 1582/1582 direkt och genom VIR.
- **Kantflyttar per slag** (nop-prob, G körningar): argument som dör på
  kanten till en blockparameter i annat register 0,38 framåt och 0,20 på
  bakkant; vanliga värden 0,16 framåt och 0,06 på bakkant; konstant som
  blockargument 0,53 (framåt).  Per funktion är de utspridda: störst
  sqlite3VdbeExec 0,09 och balance 0,06 av de tolv hetaste.  Ingen enskild
  regel tar dem; inget ändrat.
- **Ram mot clang i de heta funktionerna efter rättningen:** bara
  sqlite3PagerWrite och pcache1Fetch har ram hos oss och inte hos clang
  (shrink-wrapping).

### Looprotation i sänkningen (2026-11-12)

- **Gjort (`emit/vir/a64.c`, `vl_order`):** blocken sänks i en egen ordning
  (`L->ord`; `vl_next` och `vl_body` var de enda ställena som antog VIR:s
  ordning).  En loop vars huvud är ett test (`brif` efter högst sju enkla
  instruktioner, inga anrop) får huvudet efter sitt sista block när det
  blocket slutar med `br` till huvudet: latchen faller in i testet och testets
  hopp går tillbaka till kroppen, ett hopp per varv i stället för två.
  Kanterna in i loopen får en kopia av testet (`vl_goto`), som LLVM:s
  rotation med vakt.  `VCC_VIR_ROT=0` ger VIR:s ordning.
- **Två varianter mätta** (sqlite speedtest1, instruktioner):
  bara omordning med ett hopp till testet på vägen in **+0,07 %** (sqlites
  loopar är korta: hoppet in kostar mer än varven sparar); med kopian
  **−0,44 %** (25,94 G → 25,83 G).  Statiskt +1,5 % instruktioner (285 802 →
  289 983), `b` 16 653 → 15 388.  Kopian av ett block med arbete i, som
  prövades förut vid latchen och ströks, går alltså ihop här, där den ersätter
  ett hopp som annars körs en gång per loop.
- **Tid:** sqlite parat (sex körningar, last 10–12): −0,5 % cykler
  [−1,0 .. +3,5] mot bygget före; mot clang -O2 **1,083x** [1,077 .. 1,094]
  cykler, 1,090x instruktioner.  TSVC genom VIR: 6,42 G → 6,31 G cykler
  (−1,7 %), 151/151 kontrollsummor.
- **Tester:** nytt `vcc/vir_loop_rotate.c` (0, 1 och många varv, nästlat,
  continue/break, loop nådd från två grenar och med `goto` förbi testet);
  C-sviterna 2093/2093 direkt och 2090/2093 genom VIR med kontrollen (de tre
  kända), inline 85/85, vcc-cfi genom VIR 1582/1582, volatile 32/32, vcc-abi
  `--host` genom VIR 4 fel (de kända fyra enligt ovan; inte jämfört mot ett
  bygge utan ändringen), sqlite samma hash.
- **Sett på vägen, inte åtgärdat:** en lövfunktion med vektorinstruktioner
  får ramrekord (vektorposterna är `I_RAW`, alltså `OP_ALL` för `opt_regs`).

### Vad som körs, mot clang, per instruktionsslag; kopierade sammanflödestest (2026-11-12)

- **Verktyg:** `bench/sqlite/mix.py`: en `nop` framför varje instruktion av ett
  slag i kompilatorns `.s`, och skillnaden i instruktioner är antalet
  körningar.  Fungerar på clangs `.s` också, så blandningen kan jämföras
  exakt (summan av skillnaderna nedan är 2,16 G mot uppmätt gap 2,13 G).
- **sqlite speedtest1 `--size 100`, G körda instruktioner** (VIR efter
  looprotationen 25,82 G, clang -O2 23,69 G):

  | Slag | VIR | clang | Skillnad |
  |---|---|---|---|
  | laster (inte sp/fp) | 4,42 | 3,89 | **+0,52** |
  | villkorliga hopp | 2,87 | 2,51 | **+0,36** |
  | `mov` r,r | 1,53 | 1,18 | **+0,35** |
  | `b` | 0,91 | 0,62 | **+0,29** |
  | add/sub | 2,77 | 2,59 | +0,18 |
  | cmp/tst | 1,70 | 1,58 | +0,12 |
  | sp/fp-laster | 1,28 | 1,15 | +0,12 |
  | sp/fp-lagringar | 1,12 | 1,01 | +0,12 |
  | lagringar (inte sp/fp) | 1,12 | 1,00 | +0,12 |
  | anrop + ret | 0,65 | 0,57 | +0,08 |
  | konstanter (mov #, adrp) | 0,99 | 1,11 | −0,13 |
  | csel-familjen | 0,10 | 0,19 | −0,09 |

  Stacktrafiken är alltså nästan clangs (+0,24 G av 2,13); det största är
  vanliga laster (inga laster hissade ur loopar, färre återanvända över block,
  inlining), sedan hopp och flyttar.  Per funktion sticker
  sqlite3BtreeIndexMoveto ut (`b` 0,08 mot 0,01, `mov` 0,13 mot 0,07) och
  sqlite3VdbeExec i `b` (0,39 mot 0,27); VdbeExec har färre `mov` än clang.
- **Gjort (`vl_small` i `emit/vir/a64.c`):** ett hopp framåt till ett block
  som bara testar (`brif` med högst en instruktion före, t.ex. `if (c<0)` efter
  tre grenar som räknar ut `c`) ersätts av en kopia av blocket, som LLVM:s
  svansduplicering vid sin standardstorlek.  Vägen till testets hoppsida
  kostar då ett hopp i stället för två.
- **Mätt:** 25,84 G → 25,76 G instruktioner (**−0,31 %**), cykler bättre i
  tre parade körningar av tre (−0,5 till −1,8 %), kod +2,7 % (289 983 →
  297 728).  Större block: 3 instruktioner −0,44 % för +3,9 %, 4: −0,52 % för
  +5,1 %, 6: −0,59 % för +6,5 %; valt 2.  Bara kopior som faller igenom
  efteråt: ingen skillnad alls (vinsten ligger på hoppsidan).
- **Tester:** nytt `vcc/vir_join_dup.c`; C-sviterna genom VIR med kontrollen
  2091/2094 (de tre kända), vcc 291/291 direkt, inline 85/85, vcc-cfi genom
  VIR 1582/1582, volatile 32/32, TSVC 151/151 (6,32 G cykler), sqlite samma
  hash.

### Ramar mot clang per funktion: syskonanrop som returnerar pekare, FP-löv (2026-11-12)

- **Mätt exakt** (en räknare framför varje `stp x29, x30` i `.s`, summerad
  vid avslut): VIR satte upp 172,9 M ramar på speedtest1 `--size 100`, clang
  134,0 M.  Störst: pcache1Fetch 6,3 M, sqlite3_value_text och sqlite3ValueText
  5,8 M var, sqlite3PagerWrite 4,1 M, dekkerMul2 3,7 M, applyAffinity 3,5 M,
  sqlite3DbMallocRawNN 3,4 M, sqlite3_free 3,2 M (clang 0 i alla).
- **Det mesta var inte shrink-wrapping utan missade syskonanrop:** en funktion
  som returnerar en pekare fick aldrig något.  `ret` namnger anropets värde
  under dess `i2p` (sänkningen läser igenom `i2p`/`p2i`), och `vl_sibling`
  jämförde med värdet ovanpå.  Nu jämförs värdena utan sådana instruktioner
  (`vl_under`).  sqlite: 25,755 G → 25,670 G instruktioner (**−0,34 %**), 23
  funktioner utan ram, 172,9 M → 149,0 M ramar.
- **FP- och vektorinstruktioner som `I_WORD`** gav `OPF_ALL` i `opt_regs` och
  därmed ramrekord i varje lövfunktion med flyttal (sqlite3_value_int64,
  dekkerMul2) eller vektorer (TSVC).  I VIR:s funktioner avkodas ordets
  registerfält nu som allmänna register (en övermängd; grenar, undantag och
  fält 31 utanför FP-klassen ger fortfarande `OPF_ALL`).  −0,06 %, 12
  funktioner; TSVC 6,32 G → 6,29 G cykler.
- **Kvar som är riktig shrink-wrapping** (ett callee-saved register och ett
  riktigt anrop på en väg, tidiga returer eller syskonanrop på de andra):
  sqlite3PagerWrite 4,1 M, applyAffinity 3,5 M, sqlite3_free 3,2 M,
  sqlite3DbMallocRaw 1,9 M, sqlite3VdbeMemStringify 1,3 M, sqlite3Step 1,1 M:
  ca 17 M ramar à ca 8 instruktioner, 0,5 %.
- **Tester:** nytt `vcc/vir_tail_ptr.c`; C-sviterna genom VIR med kontrollen
  (de tre kända felen), inline 85/85, vcc-cfi genom VIR 1582/1582, vcc-abi
  `--host` 4 fel både med och utan VIR (alltså inte härifrån), TSVC 151/151,
  sqlite samma hash.

### Shrink-wrapping (2026-11-12)

- **Gjort, tre delar (SPEC 15.12):**
  1. *Allokeraren* (`save_point` i `vir/alloc.c`, på begäran genom
     `vir_aopt.shrink`): sparpunkten S är blocket som dominerar alla block som
     behöver ramen (anrop som inte är syskonanrop, memcpy/memset, asm,
     `va_start`, en plats adress, `VB_ADDR`), ligger utanför alla loopar och
     dominerar allt som nås från det; block som bara returnerar undantas.
     Framför S ges inga callee-saved register när det går att undvika, och en
     parameter flyttas inte dit vid sin definition.  Vid S flyttas de värden
     som lever över anrop till callee-saved (redigeringar före S:s första
     instruktion).  `vir_alloc_out.spoint`.
  2. *Sänkningen* (`emit/vir/a64.c`): blocken framför S läggs först, S får
     markören `NOP_PROLOG` efter sin etikett, returer och syskonanrop framför S
     märks (`I_TAIL.sh`).  Kod bakom S som går till ett returblock framför S
     får en egen kopia av det (`vl_lbl`, `VLT.lbf`).  Sänkningen säger före
     allokeringen vilka anrop som blir syskonanrop (`vl_sibcand`).
  3. *Ramen* (`finish_function`, `opt_noframe`): prologen läggs vid markören
     när posterna framför den inte anropar något, inte rör någon ramplats och
     varken läser eller skriver x19–x30; annars överst som förut och märkena
     stryks.  Inte med avvecklingstabeller (Linux `-fasynchronous-unwind-tables`),
     `-g`, VLA/alloca eller överjusterad ram.  `VCC_VIR_SHRINK=0` stänger av.
- **Mätt, sqlite speedtest1 `--size 100`:** 25,645 G → 25,514 G instruktioner
  (**−0,51 %**); cykler inom bruset i tre parade körningar.  Statiskt lika
  många instruktioner (297 581 → 297 580).
- **Fel på vägen:** ett block med bara `br` hoppades över av sänkningen
  (`vl_lonebr`) också när det var sparpunkten och bar flyttarna till
  callee-saved: sqlite3WalkSelect läste parametrarna ur register de aldrig
  flyttats till.  Allokeringskontrollen såg det inte (allokeringen var rätt);
  ett block med redigeringar hoppas inte över längre.
- **Tester:** nytt `vcc/vir_shrink_wrap.c` (kört mot clang på macOS, där
  shrink-wrapping är på); vcc-sviten 293/293 direkt och 290/293 genom VIR med
  kontrollen (de tre kända), vir-alloc 19/19, vir-test 126/126, sqlite genom
  VIR med kontrollen, samma hash.  **Inte körda efter den här ändringen:**
  hela C-sviten (gcc/tcc-delarna), inline, vcc-cfi, vcc-abi, TSVC, slumpade
  allokeringar; körningen avbröts.
- **Sammanlagt på grenen**, instruktioner mot clang -O2 (23,70 G): efter
  sammanfogningen med main 1,105x, nu 1,077x (25,51 G).

### Vad som är kvar av gapet mot clang, per instruktionsslag (2026-11-12, efter shrink-wrapping)

`bench/sqlite/mix.py`, klasserna sida vid sida (en minut; varje tal ca 0,03 G
för högt, lika för båda).  VIR 25,55 G, clang -O2 23,72 G: gap **1,84 G
(7,7 %)**.  G körda instruktioner:

| Slag | VIR | clang | Skillnad | Vid förra mätningen |
|---|---|---|---|---|
| laster (inte sp/fp) | 4,45 | 3,99 | **+0,46** | +0,52 |
| `mov` r,r | 1,59 | 1,21 | **+0,38** | +0,35 |
| villkorliga hopp | 2,93 | 2,63 | **+0,30** | +0,36 |
| `b` | 0,90 | 0,64 | **+0,26** | +0,29 |
| add/sub | 2,77 | 2,58 | +0,19 | +0,18 |
| cmp/tst | 1,74 | 1,61 | +0,13 | +0,12 |
| lagringar (inte sp/fp) | 1,15 | 1,04 | +0,12 | +0,12 |
| sp/fp-lagringar | 1,13 | 1,04 | +0,09 | +0,12 |
| sp/fp-laster | 1,27 | 1,19 | +0,08 | +0,12 |
| anrop + ret | 0,72 | 0,61 | +0,11 | +0,08 |
| konstanter (mov #, adrp) | 1,04 | 1,23 | −0,19 | −0,13 |
| csel-familjen | 0,14 | 0,20 | −0,06 | −0,09 |

Summan av skillnaderna är 1,87 G.  Ramar (`stp x29, x30`): VIR 0,11 G, clang
0,16 G: vi sätter nu upp färre ramar än clang.  Stacktrafiken är +0,17 G
(var +0,24).  Kvar i ordning: vanliga laster (byggaren: inga laster hissade ur
loopar, färre återanvända över block, inlining), `mov` (allokeraren:
blockparametrar och argumentkopior), villkorliga hopp (inlining, färre
`csel`), `b` (blockplacering).

### Flyttar: tre regler prövade utan mätbar vinst; main efter #125 (2026-11-12)

Gren `claude/vir-moves` på main efter #126.  sqlite speedtest1 `--size 100`,
instruktioner; bruset mellan körningar är ca ±0,005 G.

- **Main efter #125 är 25,71 G, grenen före var 25,52 G.**  #124 ändrar inget
  (26,178 mot 26,181 på d0cbdcc7); #125 ger +0,19 G (+0,7 %): if-konverteringen
  i byggaren gör nu upp till åtta `select` per sammanflöde i stället för en,
  så båda armarna räknas oftare.  Cyklerna är oförändrade (sex parade
  körningar: −0,1 % [−0,5 .. +0,9]).  Vektoriseraren är det inte
  (`VCC_VIR_VEC=0`: 25,70 G).  Jämförelser i instruktioner mot clang utgår
  alltså från 1,084x nu, inte 1,077x.
- **Prövat mot `mov` (+0,38 G mot clang), inget av det behållet:**
  1. En blockparameter som bara returneras tar returregistret, och dess
     argument hintas dit: −0,003 G, statiskt +85 instruktioner.  Fallet som
     motiverade det (sqlite3DbMallocRawNN: `mov x2, x0; mov x0, x2`) var redan
     löst av syskonanropen.
  2. Kopian i argumentregistret behålls när ett värde flyttas till
     callee-saved vid sparpunkten: −0,006 G.
  3. Ett registers reservation: registret för en parameter vars argument
     blocket definierar längre ned hålls ledigt till dess: −88 `mov`
     statiskt, dynamiskt inget.
- **Kantflyttarna per slag av värde** (nop-prob, G körningar; totalt ca 0,52):
  framåtkant: argument definierat i föregångaren 0,14 (därav 0,09 där
  parameterns register var känt men upptaget, 0,06 där argumentet satte det
  tentativa registret och parametern ändå hamnade annorstädes), argument som
  är en blockparameter 0,06, definierat tidigare 0,06, från anrop eller
  parameter 0,05, vanligt värde 0,09; bakkant: argument definierat före
  latchen 0,10, vanligt värde 0,05.  "Upptaget" är alltså ett värde som redan
  låg där vid blockets början (typiskt variabelns gamla värde som ännu
  används), inte ett som tog registret under tiden: det löser ingen hint.
- **Var resten av `mov`-gapet troligen ligger:** anropen.  Vi kör 0,40 G
  anrop mot clangs 0,34 G, och varje anrop kostar argumentkopior av levande
  värden (0,73 G när det mättes) och flyttar till callee-saved: 0,06 G fler
  anrop à ca tre flyttar är ungefär hälften av gapet.  Det följer inliningen.

### Tolkens `break`-block kopieras in i fallen (2026-11-12)

- **Orsak till `b` i sqlite3VdbeExec (0,39 G mot clangs 0,27 G):** 201 av
  funktionens 1181 `b` gick till ett block med `add x25, x25, #24; b` (`pOp++`
  och hoppet tillbaka till dispatchen).  Varje opcode körde alltså två
  ovillkorliga hopp, clang ett.
- **Gjort (`vl_small`):** ett hopp framåt till ett block med högst en
  instruktion före sitt `br` ersätts av en kopia av blocket, som redan gällde
  för block som slutar i `brif`.
- **Mätt, sqlite speedtest1 `--size 100`:** 25,720 G → 25,574 G instruktioner
  (**−0,57 %**), kod +1,05 % (295 592 → 298 695); cykler −1,4 %, −0,5 %, +0,1 %
  i tre parade körningar.  Mot clang 1,078x i instruktioner.
- **Tester:** nytt `vcc/vir_break_dup.c`; C-sviterna genom VIR med kontrollen
  2102/2102 (de tre switch-felen är borta på main), inline 85/85, vcc 299/299
  direkt, TSVC 151/151, sqlite samma hash.
- **TSVC på main är 7,37 G cykler, var 6,29 G före #125** (77 vektoriserade
  loopar mot 43).  Samma tal med och utan den här ändringen, med
  `VCC_VIR_SHRINK=0` och med `VCC_VIR_ROT=0`: det kommer inte härifrån och är
  inte undersökt.

### TSVC efter #125: sökloopar (2026-11-12)

TSVC genom VIR gick från 6,26 G cykler (de4e5a65) till 7,37 G på main.  Per
loop (`vcc-tsvc.py -v` med båda kompilatorerna) låg allt i fem loopar: s314 och
s316 (största och minsta värde: 0,026 och 0,019 s → 0,172 och 0,171), s315,
s3110 och s13110 (största värde med index: 0,005 → 0,035).  Två orsaker, båda
från #125:

- **Ordnad reduktion ensam vektoriserades:** vektorloopen läste fyra element
  och tog sedan ut dem ett i taget med jämförelse och val i ordning, alltså
  den skalära loopen med en vektorlast framför.  En loop utan lagring och utan
  packad summa vektoriseras inte längre på arm64 (`vec.inc`; x86-64 lämnat som
  det skrevs, inte mätt här).  7,37 G → 6,60 G.
- **If-konverteringen** gör nu `select` också när armen bara laddar om det
  villkoret läste (`vw_armreuse`) och för flera värden per sammanflöde.  I en
  sökloop blir det en kedja där varje varv väntar på det förra, där hoppet
  förut nästan aldrig togs.  Värden som vart och ett antingen behåller en
  blockparameter eller ersätter den förblir nu ett hopp (`vw_ifconv`).
  6,60 G → 6,21 G.  sqlite speedtest1: 25,56 G → 25,49 G instruktioner,
  cykler oförändrade.
- Med bara flera värden undantagna hamnade s314/s316 på clangs nivå (0,053 s);
  clang konverterar dem också.  Hoppet är snabbare på TSVC:s data.
- **Tester:** nytt `vcc/vir_search_loop.c` (lika värden, −0, NaN);
  C-sviterna genom VIR med kontrollen 2103/2103, inline 85/85, vcc 300/300
  direkt, TSVC 151/151, sqlite samma hash.
- **x86-64 (2026-11-13):** regeln i `vw_ifconv` gällde först alla mål och fällde
  `bench/vcc-vir-x86.py` i CI (tsan-jobbet kör på x86-64): testet kräver att
  maxval, maxindex och lastnegative vektoriseras där, som ordnade reduktioner
  av just de `select` regeln tog bort.  Regeln gäller nu inte x86-64, liksom
  den i `vec.inc`.  Om looparna är snabbare så på x86-64 är inte mätt.

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
