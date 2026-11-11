# VIR (Vroom IR): SSA-nivån ovanpå vandraren

**Mål:** en liten, snabb IR i SSA-form för det som inte går utan SSA: riktig
registerallokering, global värdenumrering och minnesvidarebefordran över block.
Den ska användas av x86-64 och arm64 (wasm senare), kosta ingenting på `-O0`
och inte upprepa LLVM IR:s kända misstag.  Formatet och verktygen är specificerade i
`SPEC-vir.md`; underlaget finns i
`RESEARCH-vir.md` (LLVM IR:s problem med källor, och vad B3, Cranelift, Zig,
Go, Turboshaft, LuaJIT, QBE och TPDE valde i stället).

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
4. **Allokera:** linjär genomsökning över SSA-intervall (heltals- och
   flyttalsklass, callee-sparade register över anrop, parallella flyttar för
   blockargument, fasta register för `div`, skift och argument).  Inget LICM i
   1.0.
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
| V3 | **Täckning.** Bit-fält, poster i anrop, varargs, atomära, flyttal, switch, beräknad goto, asm och asm goto | Täckning ≥ 99 % av x86-kärnans funktioner, ≥ 95 % av PostgreSQL:s; x86-kärnan bootar med `VCC_VIR=1`; objtool utan varningar |
| V4 | **Registerallokering.** Linjär genomsökning, fasta register, parallella flyttar | Instruktioner på en fast TU-mängd under dagens `-O2`; allokerarens tid redovisad för sig |
| V5 | **Pass.** Förenkling, värdenumrering, minnesvidarebefordran, död kod | Per pass: tolkekvivalens, instruktioner och `vbench.py`-tid före och efter, i planen |
| V6 | **arm64.** Kräver att arm64 har en `LO`-sänkning (TODO.md "arm64 and wasm on the shared walker"); VIR driver flytten eller väntar på den | ctests 1729/1729, `vcc-abi.py`, arm64-kärnan bootar med `VCC_VIR=1`; `vcc-obj.py` 840/840 |
| V7 | **VIR är standard på `-O2`.** | Förslag: ≤ 1,25× clang -O2 i statiska instruktioner; steg 2-självbygge ≤ 1,5× långsammare än clang-bygget (2,6× i dag); `-O2`-kompilering ≤ +15 % totalt; `-O0`/`-O1` byte-identiska hela vägen |
| senare | `NSW` och typtaggar används; SROA av oflydda poster; `returns_twice` med onormala kanter; VLA; `__int128`; wasm | — |

"VIR 1.0" är formatet och semantiken så som de ser ut när V7 passerar.

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
- **`LO` med begärt resultatregister.**  I dag väljer målet själv register
  (`treg`) och returnerar ett `val`.  Sänkningen från VIR måste kunna säga
  vilket register resultatet ska till, annars blir det en `mov` per operation
  som `rec/copies.c` får städa.  Det är V2:s största gränssnittsfråga.
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
- Grenen `claude/ir-design-compiler-lessons-ba8931` ligger 566 commits efter
  `main`; en sammanslagning ger konflikter i `TODO.md` och `cc/ccint.h`.
  AGENTS.md och PLAN-release.md är därför inte ändrade här: raderna dit
  (Layout: `PLAN-vir.md`, `RESEARCH-vir.md`; G5 pekar hit) läggs till när
  filerna kommer in på `main`.
- Ingen kod ändrad.
