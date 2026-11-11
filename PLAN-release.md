# vcc 1.0: första release

**Mål:** vcc släpps som en snabb C-kompilator som går att använda direkt via
`CC=vcc`, under MIT-licens.  Den riktar sig till dem som vill ha en snabbare
kompilator med moderna felmeddelanden.  Kärnbygget (vroom) är vår
referensanvändare, inte produkten.

**Beslut (2026-11-09, frågerunda med Johan):**
- Produkt: vcc som C-kompilator (vcc/vas/vld + vccd).
- Kvalitetsribba: TODO.md tier 1–3 tomma (tier 4 får finnas kvar som kända begränsningar).
- Språk: C upp till C23, med GNU-tillägg.
- Kod: rimlig -O1 (registerallokering, enkel inlining, konstantpropagering).
- Diagnostik: bättre än clang; clang-paritet är golvet.
- Plattformar: macOS arm64, Linux arm64, korskompilering mac→Linux, Linux x86-64
  (ELF, SysV), macOS x86-64 (Mach-O) och wasm.
- Drop-in: `CC=vcc` räcker. configure, meson och cmake går igenom, och okända
  gcc/clang-flaggor ger varning, inte fel.
- vccd: default och transparent.  Shimmen startar servern själv.
- Fart: publicerad jämförelse mot tcc och clang.
- Distribution: källkod, förbyggda tarballs, Homebrew, Debian/apt, container.
- Licens: MIT.
- Styrning: releasen styrs av kriterier, inte datum.  Agenterna arbetar som
  nu.  Grindarna nedan är det gemensamma målet.

## Grindar

Releasen är klar när alla grindar är gröna på alla plattformar som grinden
gäller.  Varje grind ska ha ett skript med tydligt grönt/rött resultat
(se G0).

| # | Spår | Grind (mätbar) |
|---|------|----------------|
| G0 | Grindrigg | `bench/release-gate.py` kör G1–G9 (eller läser deras senaste resultat) och skriver en statustabell per plattform; CI kör den på varje plattform |
| G1 | Korrekthet | TODO.md tier 1–3 tomma, inga `xfail` kvar som pekar dit. Csmith och YARPGen differentiellt mot clang: N CPU-timmar per mål och optimeringsnivå utan diff |
| G2 | Korpus | Linux-kärnan bootar Debian till `running`; PostgreSQL `meson test`, SQLite (TCL-testerna) och vcc självbyggd (stage 2 = stage 3) gröna; Debian-urvalet bygger och klarar sina egna tester med `CC=vcc` |
| G3 | Drop-in | autoconf, meson och cmake konfigurerar med `CC=vcc` för hela korpusen utan projektpatchar. Okända flaggor ger varning. Flaggtabell mot gcc och clang (`bench/vcc-driver.py` utökad) |
| G4 | Plattformar | G1–G3 körs på varje mål: macOS arm64, Linux arm64, mac→Linux arm64, Linux x86-64, macOS x86-64, wasm. ABI-tester (`bench/vcc-abi.py`) clang/vcc × anropare/anropad på varje mål |
| G5 | Kodkvalitet | `-O1`: körtid inom X % av clang -O1 på en fast benchmarksvit; `-O0` oförändrat snabbt att kompilera |
| G6 | Diagnostik | `bench/vcc-diag.py` utan regressioner mot clang, och en egen svit med fall där vcc är bevisat bättre än clang (spann genom makron, fix-its, förklaringar) |
| G7 | Fart | `bench/` publicerar kompileringstid per TU och för hela projekt: vcc mot tcc och clang -O0 (vcc -O0) och mot clang -O1 (vcc -O1), både kallt i egen process och med vccd. Siffrorna står i README |
| G8 | vccd som default | TODO.md tier 2 tom. Autostart, versionskontroll mellan shim och server, fallback till ren process om vccd saknas eller kraschar. `vccd-test.py` + `vcc-stress.py --vccd` körs i timmar; fungerar i CI och containrar |
| G9 | Paketering | En tagg ger via CI: källtarball, binära tarballs per plattform, Homebrew-formel, .deb, containeravbild. README, LICENSE (MIT) och THIRD-PARTY med ursprung och licens för clang-tabellerna (`caps.c`, `ppdata*.inc`), gcc c-torture och tcc tests2 |

## Ordning

Ordningen följer beroenden, inte datum.  Spår som inte rör samma filer
kan gå parallellt.

1. **G0 grindrigg och CI.** Den gör status synlig för alla agenter.  Först
   samlas de befintliga testskripten, sedan läggs nya grindar till när de
   finns.
2. **G9 juridik.** LICENSE, THIRD-PARTY och kontroll av att genererade
   tabeller och importerade testsviter får distribueras under MIT-projektet.
   Det är billigt nu och dyrt senare.
3. **G1 tier 1 + differentiell fuzzning**, parallellt med **x86-64-backenden**
   (`arch/x86_64/`; katalogstrukturen är förberedd).
4. **G8 tier 2 och vccd som default.**
5. **Tier 3** (front end, C23, driverflaggor för G3).
6. **G5 -O1**, först när tier 1 är tom.  En optimerare ovanpå felaktig kod
   gör buggarna svårare att hitta.
7. **wasm-målet, Debian-urvalet (G2), G6:s egna svit och G7:s mätningar.**
8. **G9 paketering**, och sedan taggen.

## Öppna frågor

- **wasm:** menas en wasm-backend (wasm32 eller wasm64, WASI?) eller vcc
  körd som wasm (`ccs/wasm/`, finns redan)?
- **C23 fullt:** `_BitInt` med godtycklig bredd, `#embed`, `constexpr`: hela
  standarden eller clangs nivå?
- **-O1:** vilken benchmarksvit, och vilket X i G5?
- **Debian-urvalet:** vilka paket och hur många?  Fast lista eller topp N
  efter popcon bland rena C-paket?
- **"Bättre än clang":** vem bedömer det?  Kuraterad svit eller A/B-test
  med användare?
- **vccd som default:** beteende i CI och containrar, och socketens
  säkerhetsmodell på delade maskiner.
- **Namn:** "vcc" krockar med Microsofts VCC och verifieraren VCC.
- **Grindägare:** ska varje grind ha en ansvarig session?
- **Csmith/YARPGen:** hur många CPU-timmar är N?

## Status

### Status 2026-11-11: G2/G7, PostgreSQL på Linux

PostgreSQL-harnessen kan nu isolera käll- och byggträd på Linux, förbereda
hela kommandodatabasen och kontrollera att varje kompileringsjobb verkligen
lyckades innan hastighet jämförs. Snabba kontraktsprov körs i CI utan en
PostgreSQL-checkout. G2 och G7 är fortsatt öppna: harnessen ensam bevisar
varken att aktuell main bygger PostgreSQL eller att dess prestandamål uppnås.

### Status 2026-11-11: G5/G7, wasm-lagringar över strukturerade joins

På `1d8e29a` använder wasm32/64 den gemensamma budgeterade flödeslösaren
för privata exakta 4/8-byte ramceller. Typade kontrollhändelser och unika
blocktoken beskriver flödet utan textparsning eller SSA. Överskrivna stores
tas bort genom strukturerade joins; operandutvärderingar och riktiga
barriärer bevaras. Loopar, dispatchers/goto, asm, SJLJ och dynamiska ramar
avstår. Full konvergens krävs före publicering.

114 fokuserade/befintliga SDK-fria nivå-/mål-/regelkontroller passerar;
avsiktligt felaktig barriärsammanslagning avvisas på wasm32 och wasm64.
Oberoende review är klar. Hela egna SDK-sviter avslutar med status 0:
wasm32 178/178 PASS, 36 XFAIL, 16 skips; wasm64 182/182 PASS, 37 XFAIL,
11 skips. Undantagen räknas inte som PASS; kategorier och loggar står i
`PLAN-fas2.md`/`PLAN-wasm.md`. CI-steget är tillagt, fjärrkörning återstår.
Senare integration `1458a90` mot exakt flödesförälder `f7327bd` passerar
samma 114 kontroller och 66 byteidentiska O0/noopt-par. Matchade clang 23
v3-byggen ger +0,314–0,552 % Callgrind-Ir i oförändrade kontroller,
+0,106/+0,095 % i fast wasm32/64-blandning, och +4,494–4,683 % i
koncentrerade positiva diamantprov. De senare minskar stores 800/1000
till 200 och relocerbara objekt 2,886–3,325 %. Aktiv analys kostar mer än
1 % där; resultatet ersätter inte full prestandagrind eller uppmätt
programfart. Exakta korpusundantag och metod står i planerna. G5/G7,
generell DSE och full O2 förblir öppna.
### Status 2026-11-11: G1/G4, semantisk följdmilstolpe ovanpå native-stacken

Fyra separata rättningar bygger vidare på verklig Git-förälder `74f6ec4`:
livstid för deklarationer efter etiketter, oberoende nästlade GNU-ranglistor,
C90:s digrafer/satsscope med blandade cachejobb samt full bredd i generiska
rotationer. Inga kompletta C23-recordkompatibilitetsändringar ingår.

Unity och filvis bygge: egna x86-prov 235/235 med sex referensundantag,
ARM/QEMU 240/240 med ett befintligt xfail; ABI 33/33 per mål/bygge;
diagnostik 225/225. Kernel-sema run/batch 887/887 på båda byggena.
Full PP-paritet är 920/920 i cmp/batch både före ändringen på föräldern
och efteråt (unity). Fyra riktade oförändrade GCC-fall passerar per mål/bygge;
C90-orakeln kör sju lägen och 21 blandade cachejobb. Fem fokuserade runtimeprov
passerar även O2/O3. ASan/UBSan passerar diagnostik, tre nya runtimeprov och
C90-matrisen på båda Linuxmålen. Wasm64-preflight: 191 pass, 39 befintliga
xfail, 11 referensundantag; inga XPASS-markörer behövde ändras.

Nio utvalda Kefir-fall passerar alla kombinationer på x86; ARM ger fem pass
och fyra explicita Clang-referensavvisningar (wchar-/bredtypskontrakt).
Detta är riktad återvalidering, inte en ny full S4/S5-körning. Artefakter,
bundle med bevarad Git-historik och manifest: `work/results/semantic-stack-74/`.
G1/G4 är fortfarande öppna.

### Status 2026-11-11: G1/G4, rotationer över 128 bitar

På basen `a41361e` behålls operandens och räknarens fulla bredd före modulo
för generiska rotationer. GCC:s oförändrade `builtin-stdc-rotate-1.c` behåller
`-std=c11`; dess 510-bitarsdel körs bara på x86 (ARM annonserar max 128).
Riktad verifiering: unity/separatfilbygge, O0/O2/O3 med typer, sidoeffekter och
stora räknare, plus semantik run/batch. Resultat:
`work/results/stdc-rotate/validation.json`. Clangs begränsning för dynamiska
rotationer över 128 bitar är separat dokumenterad; det är inte ett pass.
Hela S4 har inte körts om och G1/G4 är fortfarande öppna.

### Status 2026-11-11: G1/G4, livstid för deklarationer efter etiketter

På `a41361e` rättas frontendens syntetiska scope runt deklarationer efter
etiketter. Detta eliminerar överlappande levande ARM-stackplatser och
för tidig cleanup även på x86. Kefir std_attributes18 klarar alla fyra
kombinationer på båda Linux-målen och byggen; egna sviter, sema och diag
är gröna med befintliga undantag. Se `TEST.plan` S5b och
`work/arm-computed-goto/` för avgränsade resultat. G1/G4 är fortsatt öppna.

### Status 2026-11-11: G1/G4, kombinerad native-stack ovanpå 1c5b5d1

Sju separata native-rättningar har kombinerats och validerats på exakt
stacktoppen `1c5b5d1`. Unity/filvis: egna Linux-sviter x86 232/232 och ARM
237/237 med befintliga referensundantag/xfail, ABI 33/33 och atomik 3/3.
PP-paritet cmp/batch är 920/920; sema run/batch 887/887 på båda byggen.
Sex fokuserade Kefir-fall passerar alla fyra kombinationer på x86;
ARM:s tre aktiva fall passerar medan två Clang-avvisningar och x86-vakten
redovisas separat. ExtInt- och alloca-kantfall samt GCC pr113596 är gröna;
breda strängars tidigare x86-assertionsfel är nu rättat. Se `TEST.plan`
S5b och `work/native-stack64/` för exakt evidens och återstående gränser.
Ingen full Kefir-/GCC-grind eller ytterligare plattformstäckning påstås;
G1/G4 är fortsatt öppna.

### Status 2026-11-11: G1/G4, livstid för inlinad alloca

På basen `103cdee` återställs stacken vid den inlinade funktionens gemensamma
returpunkt på Linux x86/ARM, efter argumentens evaluering. Anroparens VLA-
sparpunkter ändras inte av den inlinade funktionens alloca. GCC:s oförändrade
`gcc.dg/pr113596.c` behåller `-O2`; regressionen täcker nästlad inlining,
tidiga returer och levande VLA/alloca i anroparen, även vid `-O0`.
Resultat från unity/separatfilbygge, egna körsviter och semantik run/batch:
`work/results/pr113596/validation.json`. Hela S4 har inte körts om;
G1/G4 och andra mål förblir separata öppna grindar.

### Status 2026-11-11: G5/G7, wasm frame-cell forwarding

Wasm32/64 använder nu den gemensamma cell-/värdekärnan för exakta
4/8-byte frame-celler och tar bort överskrivna lagringar. Typade
emissionshändelser bevarar adress-/RHS-effekter och operandstack; inga
SSA-grafer eller assemblertextmönster. Positivt aggregatprov går från sex
till två minnesoperationer. Blandat elvafilersprov ger +0,599 % värd-Ir,
kontroller +0,18–0,30 %, positivt prov +7,03 %. Detta är avgränsad data,
inte hela G7. 16 fokuserade kör-/kodkrav, sanitiserare, oberoende review,
6120 identiska native-resultat och 149 egna wasm32-PASS verifierar steget.
Lokal wasm64-referenskrasch är baslinjereproducerad och synlig; se
PLAN-wasm.md. X86:s adapter är fortsatt experimentell på grund av kostnad;
smala wasm-celler, generell unread-DSE, full O2 och G5/G7 återstår.

### Status 2026-11-11: G1/G4, objektstorlek på Linux

På basen `9d022c3` skiljer objektstorleksanalysen deklarerade delobjekt från
rå allokering och padding. `-fstrict-flex-arrays=0..3` ingår i cacheidentiteten;
läge 3 behåller deklarerad minsta storlek. `counted_by` på stabila pekarfält
använder aktuell räknare i dynamiska lägen 0–2; volatila baser/fält lämnas
okända utan läsning. Inlining bevarar bounds bara för direkta, oskrivna och
icke-aliaserade pekarparametrar med effektfria argument.

Verifiering omfattar unity/separatfilbygge, egna körsviter på Linux x86-64
följt av ARM64/QEMU, fem oförändrade GCC-fall (`pr101836_1/2/3`, `pr39343`,
`pointer-counted-by-8`), oberoende strikt-flex/volatile/allokeringsprober och
kärnans semantik i både process- och batchläge. Exakta resultat och binärhashar:
`work/results/objsize-main-validation.json` och
`work/results/objsize-main-independent/results.json`.
Båda byggena: ARM 185/185, semantik run/batch 887/887, GCC 5/5 per mål;
34 oberoende konfigurationer gav 102 matchande körningar (272 kommandon).
Ursprunglig x86-körning: 179/179 och sex skip, varav `branch_far` var en
Clang-referenstimeout efter 60 s, inte ett godkänt compilerresultat.
Separat supplement använder PR37-harness `761142b` och dess uttryckliga
300 s-kontrakt: 1/1 utan skip på båda mål med båda byggena. Resultat sparas i
`work/results/objsize-main-branchfar/results.json`. Ursprungliga loggar ändras inte.

Kvar: generell flödes-/aliasanalys, transitiva inline-parametrar,
`alloc_size`-kontrakt och dynamiska uttryck med sidoeffekter; okända bounds
ska fortsätta vara okända. Hela S4-korpusen och övriga mål har inte körts om
för denna milstolpe. Riktade Wasm32/64-prover går 10/10 med ett kört XFAIL
för `object_size_inline`: samma test ger byteidentisk assemblerkod och
samma runtimefel med tidigare `main`; Clang-referensen passerar. Testet
behöver Wasm-inlining/argumentbounds och är inte överhoppat. G1/G4 är inte färdiga.

### Status 2026-11-11: G5/G7, kopior och direkt utvidgningsbevis

Delad native kopiekärna återanvänder arm64:s analys; x86 gör endast
kandidatsökning i sin befintliga tvåadressvandring, utan egna allokeringar
eller livenesspass. Separat extensions.c utelämnar bevisat redundant
unsigned32-normalisering från föregående skrivning. 148 fokuserade
körkombinationer gröna efter integration med main9d022c3, inklusive
volatile/high-bit/signed-regressioner; båda byggsätt och sanitiserade
kontrakt verifierade. 500-funktions v3-instruktionsprober mot14379fe ger
+0,23% loop och −0,74% kontroll för kombinationen. Profilens räckvidd
är begränsad; vbench/native helkorpus och hela G5/G7/O2-målet kvarstår.
### Status 2026-11-11: G1, breda BitInt på Linux

Skalära `_BitInt` över 128 bitar har nu limb-baserad kodgenerering för
Linux x86-64 och arm64, med de befintliga snabbvägarna för högst 128 bitar
bevarade. Milstolpen omfattar heltalsaritmetik, jämförelser, konvertering
mellan heltal, bitfält, generiska biträkningar, konstantuttryck och replay.
Vektorelement över 128 bitar och switch-gränser utanför signed 128-bitars
representation avvisas uttryckligen. Flyttalskonvertering följer nedan.
Detta slutför inte C23 eller G1.

Validering av integrationen på `ae941cb`: unity och separata filer,
egna x86-prov 184/184 med fem referensundantag och ARM 189/189. Oberoende
säkerhets- och avvisningsprober passerar 36/36 över båda byggsätten och
målen. Kärnans sema passerar 887/887 individuellt och i batch för båda
byggsätten. Bred aritmetik och biträkning passerar tolv bredder 129–7001,
konstantproven 128 värden plus positiva/negativa kontrakt, replay åtta
översättningsenheter på två mål och bred ABI 24 former i fyra riktningar
per mål och byggsätt (384 körningar). Diagnostik: 220/220 per byggsätt.
Optimizerregler O0–O3 och registereffekter med ASan passerar.
Fullständiga kommandon och resultat finns i
`work/results/bitint-integration/`. ARM:s Clang 23 saknar native-stöd för
bredder över 128; ARM:s breda ABI-prov använder dokumenterade C-aggregat
som proxy och får inte beskrivas som native Clang BitInt-paritet.

**Återintegration på main `2dc4ea3`:** båda byggsätten passerar egna
Linux x86 222/222 med sex referensundantag och ARM 227/227 med ett kört
befintligt XFAIL. Full egen Wasm64-svit: 180/180, 37 körda befintliga
XFAIL och elva referensundantag. Kärnsema är 887/887 i båda körsätt och
byggen. Oberoende BitInt-prober är 36/36; dagens leaf-frame-, cell-,
kopierings- och konstantkontrollgrindar passerar på båda byggena.
Körtidskonvertering till/från flyttal är fortfarande uttryckligen
utanför denna integration. Exakta kommandon, resultat och källmanifest:
`work/results/bitint-main2dc/`. Den fulla vendrade Linux-körkorpusen
passerar därefter 1942/1942 x86 (130 undantag) och 1945/1945 ARM/QEMU
(126 undantag och ett befintligt kört XFAIL), utan cacheåteranvändning.
Hela Kefirs 875 runtime-par har också körts först på x86, sedan ARM:
x86 572 godkända (+91), 8 vcc-fel och 10 signal/timeout; ARM 455 godkända,
3 vcc-fel och 7 signal/timeout. Inga tidigare godkännanden förloras;
referensfel och unsupported är oförändrade och räknas inte som pass.
Binär SHA256 `f2a6f04804de1393357a2b4341059f8bc9cbc30c8b5361808df43f203d6e0b74`.
G1/G2 och övrig testplan är fortsatt öppna.


### Status 2026-11-11: G1, padding i BitInt-uttryck

Sammansatta tilldelningar normaliserar nu gamla `_BitInt`-värdet innan
skift, division och rest på både Linux x86-64 och arm64. Det oförändrade
Kefir-fallet `bitint_assign_shift1` passerar alla fyra Clang/vcc-parningar
på båda målen. Regressionen provar 9–128 bitar, smutsig padding, tecken och
engångsevaluering av operander. Unity och separata filer samt kärnans
sema 887/887 individuellt och i batch är verifierade för den isolerade
fixen. Den kombinerade milstolpen `04209a5` passerar 18 oförändrade Kefir-par
med fyra kompilatorkombinationer per Linux-mål (144 körningar), båda
byggsätten, diagnostik 220/220, ABI 32/32 per mål och ASan 19/19 per mål.
Egna ARM-prov: 189/189; x86: 183/183 plus ett separat godkänt `branch_far`
med PR37:s granskade 300 s-kontrakt (ursprunglig 60 s-timeout bevaras i
loggen). Fem övriga x86-referensundantag återstår. Wasm64 går 148/148 med
31 körda XFAIL och tio referensundantag; fokuserad Wasm32 5/5 med sju XFAIL.
Padding-testets Wasm-lucka är bevisad med båda tidigare baslinjerna.
Manifest och slutgrind: `work/validate-kefir-7eca264/validation.json` och
`final-manifest.json`. G1 är fortsatt öppen.

### Status 2026-11-11: G5/G7, registerlokaler på x86

Gemensamt behörighetsurval i `emit/opt/locals.c` används av alla tre
bakändar; x86 håller nu lönsamma skalärer i fyra callee-save-register.
Cold-prober är oförändrade; ett native loopprov går 7,830→6,345 ms men
divisionsfritt loopkorpus kostar cirka 11 % mer kompilator-CPU. Det är
inte en godkänd 1 %-grind. Snapshot/ABI-säkerhet för builtin setjmp och
sena/inlinade justerade ramar är testad. Båda byggsätten: 116 fokuserade
kombinationer; x86-egensvit 137/137, arm64 142/142, kors-ABI 22/22.
CI kör nu x86-prober under qemu-user. Se OPT3 i `PLAN-fas2.md` för
hashar, mätmetod och återstående arbete. G5/G7/O2-målet fortsatt öppna.

### Status 2026-11-11: G5/G7, delad stackcellanalys

Nästa del delar nu värde-/storefakta i `emit/rec/slot-values.c`. 6108
byte-identiska arm64-resultat och separata sanitiserade kontrakt verifierar
flytten. X86-/wasm-anslutning och kombinerad kostnadsgrind pågår; G5/G7
är fortsatt öppna.

Arm64 använder nu en gemensam analys av exakta allokeringsceller,
överlapp och adressflykt i `emit/rec/slots.c`. 6069 byte-identiska
objekt-/textjämförelser, 177 egna arm64-körtester och ett sanitiserat
intervallkontrakt verifierar flytten. Två begränsade instruktionsprofiler
visar +0,655/+0,303 % kompilatorarbete; metod och baser i OPT5 i
`PLAN-fas2.md`. X86-/wasm-anslutning och delade omskrivningar återstår;
G5/G7 och full O2-täckning är inte klara.

### Status 2026-11-11: G5/G7, direkt parameterbindning

Utvalda x86-parametrar binds direkt till registerhem utan extra ABI-spill
eller efterladdning. Smala typer, stackargument, mixed GP/FP, hidden sret
och varargs verifierade O0–O3; kors-ABI 22/22. Parade korta mätningar och
begränsningar finns i OPT3 i `PLAN-fas2.md`. G5/G7/O2-målet fortsatt öppna.

### Status 2026-11-11: G5/G7, onåbar kod och signed tvåpotenser

Delat linjärt record-pass tar bort säkert onåbara suffix; x86 har nu
register-/effektkontrakt för nästa kopie-/livenesssteg. Signed konstant
tvåpotensdivision/-rest delar ett recept mellan alla bakändar; wasm
behåller kompakt div/rem där clang också gör det. Separate regelfiler
och måladaptrar. Båda byggsätten klarar 96 fokuserade kontroller;
egensviter x86 135/135 och arm64 140/140. Syntetisk kompilator-CPU-kostnad
och kvarvarande osäkerhet redovisas vid OPT8 i `PLAN-fas2.md`.
G5/G7 och hela O2-målet är fortsatt öppna.

### Status 2026-11-11: G5/G7, delad grenstädning

Arm64 och x86 använder nu `emit/rec/fallthrough.c`; arm64:s extraktion
är byte-identisk för 131 befintliga testfiler med O1 och -g. Wasm
utelämnar redundanta while-grenar med separat nåbarhetsbevis. Båda
byggsätten klarar 64 fokuserade kod-/körkombinationer; egensviter x86
133/133 och arm64 138/138. Mätdata och kvarvarande CPU-osäkerhet finns
vid OPT2 i `PLAN-fas2.md`. G5/G7 och O2-målet är fortsatt öppna.

### Status 2026-11-11: G5/G7, delade heltalsidentiteter

`emit/opt/identity.c` delar nu typkorrekta heltalsidentiteter mellan alla
bakändar; tvåpotensregeln ligger separat i `divpow2.c`. Unity och
fil-för-fil klarar 32 kod-/körkontroller vid O0–O3. Egna sviter: x86
131/131, arm64 136/136. Effekter och smala/höga registerbitar täcks av
regressioner. Mätbrus hindrar slutsats om små kompilator-CPU-skillnader;
se OPT1 i `PLAN-fas2.md`. G5/G7 och hela O2-målet är fortsatt öppna.

### Status 2026-11-11: G5/G7, första gemensamma regel implementerad

Unsigned konstant tvåpotensdivision/-rest sänks nu genom samma hjälpare
för x86, arm64 och wasm32/64. Alla 16 mål-/nivåkombinationer klarar
kodkrav och literalbaserad körkontroll, även fil-för-fil. Egna sviter:
x86 130/130, arm64 135/135. Wasm-testvägen använder vcc:s egna objekt
och länkare plus systemets Node 24/25, utan Emscripten; full
runtimeersättning är W9 i `PLAN-wasm.md`. Korta före/efter-mätningar
och begränsningarna för hårdvaruräknare är dokumenterade vid OPT1
i `PLAN-fas2.md`. Detta är delprogress; G5/G7 är fortsatt öppna.

### Status 2026-11-11: G5/G7, gemensam implementationsplan

`PLAN-fas2.md` **Gemensam optimerare utan SSA** äger nu OPT0–OPT8 efter
kodgranskning av alla tre generatorer. C-regler/strategier delas av x86,
arm64 och wasm32/wasm64; arm64:s befintliga postoptimerare extraheras för
registermålen. X4 och wasm-planen äger inkoppling och målsänkning.
Planen anger beroenden, effekt-/ABI-/minneskontrakt, begränsad CPU-kostnad,
tester och separata mätningar av kompilator och genererad kod. Nästa
leverans är en ny bas och en delad heltalsregel med tre konsumenter.
Ingen optimering implementerad; G5/G7 är fortsatt öppna och G5:s X saknas.

### Status 2026-11-11: G5, audit av x86-kodkvalitet

`PLAN-x86.md` X4 har en prioriterad audit mot clang 23.1.6 och GCC 15.2.0
på Ryzen 7 9700X: lokala optimeringar med låg kompilatorkostnad utan
IR/SSA. Utökad med 29 reproducerbara prober vid O0–O3, baseline och
native: 24/24 körningar ger samma checksumma samt klarar gränsvärden och
kodkontroller för befintliga egenskaper. Flaggor, passloggar och kod sparas
av `bench/vcc-optaudit.py`; vcc O1/O2/O3 ger samma assembler här.
Inga optimeringar implementerade eller uppmätt
körtidsmål; G5 är fortsatt öppet och benchmarksvitens X är inte fastställt.

### Status 2026-11-11: x86-funktionsmål, SSE4 och matematik
G1/G2 har en sammanhängande x86-featuremodell utan LLVM vid körning:
CPU/native, funktionsattribut, inlining, SSE4-strängoperationer och interrupt.
AVX-ABI för breda vektorer och record diagnostiseras tills transporten finns.
Fast-math sänker sqrt och begränsade heltalspotenser med bevarad evaluering.
Isolerad validering på bas 0757803: x86 162/162 (fem referens-/målskip),
ARM64 167/167, ABI 23/23 per mål, PP 920/920 och sema 887/887 i båda lägena.
Featuremakron 587/587, attribut/ABI-spärrar 302/302, SSE4-grindar 124/124
samt GCC pr40470 4/4 passerar. G1/G2 och S4 är fortfarande öppna.
### Status 2026-11-11: atomiska helobjekt på Linux
Större atomiska record, heltal och komplexa värden använder nu libatomic-ABI
när den inbyggda 1/2/4/8-bytevägen inte räcker. Jämför-och-byt-loopar behåller
hela objektet och rätt förväntat värde även vid padding och konkurrens.
Pthread-prov samt atomikregressioner klarar Linux x86 och ARM i unity,
filvis bygge och ASan/UBSan; kernel-sema är 887/887 i båda körsätten.
Den nya snabba värdriggen körs i normal och ASan-CI. Atomiska vektortyper
återstår i frontend; G1/G2 är fortsatt öppna.

### Status 2026-11-11: objektrepresentationer och packade boolvektorer
`__builtin_bit_cast` har körning och bytebaserad konstanttolkning med okänd
padding; packade boolvektorer har logiska element, bitlagring och verifierad
Linux/Wasm-ABI. Isolerad unity/filvis validering är 164/164 egna x86-prov,
169/169 ARM-prov och 32/32 ABI-prov per mål. TSan och skyddssidesprov är
gröna; se `TEST.plan` S2 för gränser och validering. Detta flyttar G1/G2 men
stänger dem inte; separat `nullptr_t`-identitet återstår.

### Status 2026-11-11: externa tester på Linux x86-64
ARM64:s stackallokering och slots använder nu 64-bitars offset med kontrollerad
överflödeshantering. GCC `20031023-{1..4}` kompilerar (4/4); `frame_wide`
körs med 64 KiB under QEMU och kompilerar med 1 TiB både direkt till objekt
och via assemblertext. Isolerad egen ARM64-svit: 115/115. Detta flyttar G1/G2
men stänger inte grindarna.

G1 har fått mätbara baslinjer från clangs diagnostiktester, GCC:s
compile-svit och två små körsviter. Fel och krascher återstår; se
`TEST.plan` S2, S3 och S5. G1 och G2 är inte färdiga.

### Status 2026-11-09: plan skriven, inga grindar byggda
Inget av G0–G9 finns som skript än.  Det som finns i dag och blir delar av
grindarna: `vcc-diff.py`, `vcc-sema.py`, `vcc-ctests.py`, `vcc-abi.py`,
`vcc-diag.py`, `vcc-driver.py`, `vccd-test.py`, `vcc-stress.py`,
`bench/pg/`, `vccd-kbuild.sh` + `boot-test.py`.  Det saknas README,
LICENSE, x86-64-backend, wasm-backend, -O1, SQLite-korpus, Debian-urval,
Csmith/YARPGen-rigg och paketering.

### Status 2026-11-11: G5/G7, switchmedlemskap med bitmask

OPT6 känner igen begränsade staplade case med en gemensam destination och
sänker dem till en intervallskyddad bitmask på arm64, x86 och båda wasm-målen.
Riktade kör- och kodkontroller passerar O0–O3 och `VCC_NOOPT`; vcc-sviten
passerar 246/246 med en befintlig xfail. Ett riktat x86-exempel minskar från
131 till 85 byte. Mätningens begränsningar och kvarvarande arbete finns i
PLAN-fas2; G5/G7 och full O2 är fortsatt öppna.

### Status 2026-11-11: G5/G7, balanserad switchdispatch

OPT6:s första del använder gemensamt strategival/sortering och balanserade
jämförelseträd på x86, arm64 och wasm; täta wasm-tabeller behålls. Kombinationen
med b63c269 bygger unity/fil-för-fil och passerar 164 riktade kör-/kodkrav,
219 arm64- och 214 x86-regressioner. På fded15a-basen är 500-funktionskontroller
byte-identiska och extra Callgrind-kompileringsarbete högst +0,180 %; ett
riktat x86-switchprov minskar exekverade instruktioner 48,39 %. Avgränsning,
rådata och kvarvarande OPT6-delar står i PLAN-fas2. G5/G7 och full O2 är öppna.


### Status 2026-11-11: G5/G7, billigare gemensam cellinsamling

Arm64/wasm använder den delade strömmande intervallinsamlaren utan ändrad
kodgenerering. Matchade O1/O2-mikrofall minskar hela kompileringens Ir med
0,145–0,453 % för flera fält; ett fält kostar 0,050–0,053 % och kontroller
0,006–0,071 %. Randomiserad sanitizer-/mutationsoracle, 118 byteparitetspar,
observerbara accesser och fokuserade wasm-körfall passerar. Detaljer och
avvisade varianter finns i PLAN-fas2. Ingen bred prestandagrind eller ny
optimeringsregel ingår; G5/G7 och full O2 är fortsatt öppna.
### Status 2026-11-11: G5/G7, unsigned32 reciprokdivision

OPT8:s u32-del använder gemensamma reciproker för native kvot/rest och
begränsade wasm-kvoter. 180 riktade kör-/kodkrav, sanitiserade matematik- och
tryckkontrakt, unity/fil-för-fil samt 220 arm64-/215 x86-regressioner passerar.
Mot exakt 781ca69 ligger extra arbete i kontroller under +0,442 % och en
konfigurerad 10–12-filers mix mellan −0,017 % och +0,018 %. En mättad
arm64-divisionskorpus kostar däremot +19,54 % att kompilera; avvägningen och
avgränsade x86/wasm-körprov står i PLAN-fas2. Detta stänger inte G5/G7.
Signed/u64-reciproker och full O2-täckning återstår.

Efter integrering med main54580f7: samma kompilatorkod, 180/180 riktade fall,
221/221 arm64-regressioner (ett befintligt xfail), 216/216 x86-regressioner
(sex referensskips) samt sanitiserade reciprok-/tryckkontrakt passerar.

### Status 2026-11-11: G5/G7, unsigned64 reciprokdivision

OPT8 har nu separata u32/u64-recept, native högprodukt med bevarade implicita
register och billig wasm-u64-jämförelsekvot. 196 riktade fall, sanitiserade
matematik-/registerkontrakt, unity/fil-för-fil, 222 arm64-/217 x86-regressioner,
ABI och unwind passerar. Mot exakt 046c6dc kostar kontroll-/loopprov högst
+0,00021 % och den konfigurerade mixen −0,00794…+0,00082 %.
Mättade u64-funktioner kostar däremot +14,10 % x86/+17,99 % arm64 att kompilera;
ett avgränsat seedat x86-körprov minskar median barn-CPU 69,55 % med samma svar.
Avvägning, exakta hashvärden och begränsningar står i PLAN-fas2. Signed,
dyrare wasm-klasser och full O2 återstår; G5/G7 stängs inte av detta delsteg.

### Status 2026-11-11: G5/G7, x86-blad utan ram

Verkligt ramfria x86-blad kan nu hoppa över rbp-prolog/epilog; policy och
begränsad effektkontroll ligger i en egen frame.c och den redan befintliga
postvandringen. Explicit frame-pointer-policy och blandade deklarationscacher
är testade. CFI/debug, hårdningsinträden och en riktig signal-unwind genom ett
ramfritt blad är verifierade; övriga funktioner behåller sin ram. Egna x86-prov
215/215, ABI 32/32 och befintliga registerhems-unwindprov är gröna.
Två kalla 501-funktions-profiler ger −0,176 %/−0,537 % Ir; kontrollens assembler
är identisk. Käll-/binärhashar, flaggor och bytebegränsningar står i PLAN-x86.md.
Ingen generell fart- eller programkörtidsvinst är bevisad. G5/G7 och hela O2
är fortsatt öppna; parameterhem och bredare mätningar återstår.

### Status 2026-11-11: G5/G7, rena konstanta kontrollbeslut

OPT6:s gemensamma kontrollhjälpare används av x86, arm64 och wasm med
respektive måls befintliga `constant_p`-policy. Det nya delsteget bevarar
O0/noopt i 64 bytejämförelser på fyra mål, passerar 164 befintliga och
16 nya riktade nivå-/målkontroller samt fil-för-fil-bygge. Två tidigare
wasm-xfail (`if_call_const`, `cond_dead_and`) är obligatoriska på wasm32/64.
Globala volatile-/atomic-laddningar och exakt en utvärdering kontrolleras
uttryckligen. Mot exakt 781ca69 med lika värdflaggor är avgränsade
500-funktionskontroller inom +0,095 % extra Callgrind-arbete; det positiva
kontrollprovet minskar kompilatorns instruktioner 2,75–18,36 % beroende på
mål. Detta ersätter inte full prestandagrind eller CI och belägger ingen
generell förbättring i programmets körtid. G5/G7 och full O2 är öppna;
rådata, återstående delar och begränsningar står i PLAN-fas2.

### Status 2026-11-11: integration av x86-funktionsmål och OPT3

Funktionsmål och packad bool-ABI delar nu klassificering med optimerarens
registerhem. SSE4-poster har explicita register-/flaggeffekter; fasta
variadiska boolparametrar verifieras även via indirekta anrop. Båda bygglägen
klarar egna Linuxsviter (176 x86, 181 ARM), ABI 32 per mål, 220 diagnostikprov
och 887 kernel-TU:er i båda semalägena. G1/G2/G5 förblir öppna.


### Status 2026-11-11: G5/G7, enoperationsrecept för gånger 3/5/9

Ett delat konstanttidsrecept väljer separata native-adaptrar: skalad LEA på
x86 och skiftad ADD på arm64, O1+ utan VCC_NOOPT. Wasm behåller mul enligt
referensens kostnadsval. O0/NOOPT/overflow-paritet, modulär sanitizer-/mutationsoracle,
posteffekter, två bygglägen och fyra måls O0–O3-körfall passerar. Kodformsvinst
är belagd; matchad mikrokostnad mot historisk bas 69c1bd7 är högst +0,126 %
för kontroller; positivt prov kostar +0,353 % x86 och −2,147 % arm64.
Wasm är byteidentiskt. Detta är kompileringens Ir, inte körtidsvinst eller full
prestandagrind; slutlig integrations-CI återstår. Faktorer
6/7/10/15 och bredare aritmetikregler är öppna. G5/G7 och full O2 förblir öppna.

Efter konfliktintegration med main4bb6d52 via OPT3-huvudet 9b18f64: unitybygge,
16/16 riktade unsigned32-fall på fyra mål samt sanitiserade reciprok-/tryck-
och registereffektkontrakt passerar. Båda planernas status behålls.

Efter mainf97d16d (#47/#52/#55): unity, 16 unsigned32-mål-/nivåfall,
16 konstanta kontrollfall och sanitiserade reciprok-/registereffektskontrakt
passerar igen; de två planernas oberoende statusblock är bevarade.

U64 integrerat med mainf97d16d/PR51head54c8f3f: 196 regelprov, 16 kontrollprov,
229 arm64-/224 x86-regressioner, ABI, unity/fil-för-fil och sanitizerkontrakt
passerar. Ny exakt föräldramätning54c8f3f→d8957b3: fixerad mix högst
+0,01955 %, mättad u64-korpus +13,55 % x86/+17,97 % arm64 kompilatorarbete;
avgränsat x86-körprov −69,26 % barn-CPU. Historiska siffror ovan och fulla
öppna G5/G7-grindar behålls; se PLAN-fas2 för hashvärden och provens gränser.

Efter main2dc4ea3 (#50:s x86-blad): unity, 16 division-/nivå-/målfall,
sanitiserade reciprok-/registereffektkontrakt och bladramens policy, cache-,
debug-/härdnings- och signal-unwindprov passerar. Båda statusblock behålls.

### Status 2026-11-11: G5/G7, begränsade native-tail-calls

Gemensam semantisk kontroll och separata måladaptrar ersätter enkla direkta
return-call-wrappers med epilog följd av symbolgren på x86 och arm64 vid
O1–O3. Flaggordning, ärvda attribut, ABI-register, CFI och arm64:s direkta
objektrelokeringar är verifierade. Standard-wasm behåller vanligt call,
liksom clang utan explicit tail-call-feature. Kärnsema/paritet på integrerad
main-bas, egna x86-/arm64-prov, båda native-ABI-sviterna och verklig unwind är gröna.
Två exakta 501-funktions-Ir-profiler visar −12,082 % för wrapperfallet och
+0,338 % för oförändrad aritmetisk kontroll; bara wrapperfallet minskar text.
Hashar, omfattning och kvarvarande undantag står i OPT8 i PLAN-fas2.md.
Detta bevisar ingen generell fartvinst. G5/G7 och full O2 är fortsatt öppna.

U64 har också integrerat main2dc4ea3 via PR51head15551a7: 196 riktade fall,
sanitiserade kontrakt och hela bladramsharnessen passerar. Reciprokfilerna är
oförändrade; mätningarna förblir bundna till sina tidigare exakta hashvärden.

### Status 2026-11-11: G5/G7, signed32/64 reciprokdivision

OPT8:s signed-del ovanpå `f585591` har gemensamma typade reciproker och
separata x86/arm64-adaptrar. Kvot och rest följer C:s avrundning mot noll,
även för negativa divisorer och INT_MIN; wasm behåller motorns signed
division/rest. Ett bevisat likhetsfall tar bort onödig täljaraddition för
bland annat `/3`. O0, `noopt` och högt registertryck behåller tidigare väg.

Unity/fristående implementationer, 16 mål-/nivåkörningar och fyra noopt-
kontroller passerar, liksom sanitiserade matematik-/tryck-/registerparskontrakt.
Native ger 230 arm64-PASS (ett befintligt xfail), 225 x86-PASS (sex synliga
referensundantag), ABI 32 per mål samt x86 debug/unwind. Parade v3-mätningar
av exakt `f585591`→`fbc7105` ger fixerad mix högst +0,00572 % extra
kompilatorinstruktioner. Mättad native signed32/64-kod kostar +14,21…+30,03 %;
ett avgränsat x86-körprov ger −63,59 % barn-CPU, med clang-identiska svar.
Åtta O0/noopt-assemblerjämförelser är byte-identiska. Fulla G5/G7 förblir öppna.
Implementations- och valideringsgränserna står i PLAN-fas2:s signed-status.


OPT1-integrationskontroll på 60027c7 efter wasm-föräldern 6d60e73:
båda bygglägen, 16 fulla assemblerpar, 16 mål/nivå-körfall, sanitiserade
recept-/postkontrakt och 36 policy-/overflow-paritetsjämförelser passerar.
Tidigare kompilatorkostnad gäller fortfarande historiska 69c1bd7 till f92b67b;
den tillskrivs inte denna nya förälder. Integrations-CI och full G7 återstår.

### Status 2026-11-11: G1, promotion av bitfältsuttryck

Smala unsigned-bitfält behåller sin promotionsbredd genom tilldelning,
sammansatt tilldelning, komma och prefixinkrement. `sizeof`/`typeof`,
postfix och explicita kast behåller sina separata deklarerade typregler.
Det rättar två oförändrade Kefir-kontrakt på Linux x86-64 och ARM64/QEMU,
alla fyra Clang/vcc-parningar. Regressionen provar även sidoeffekter.
Unity/separata filer, sema run/batch 887/887 och egna körsviter
223/223 x86 respektive 228/228 ARM är verifierade; sex x86-referensundantag
och ett befintligt kört ARM-XFAIL redovisas separat. Hela Kefir-sviten och
G1/G2 återstår. Evidens: `work/agents/kefir-bitfield/results/evidence.txt`.

### Status 2026-11-11: G1/G2, strängprefix, embed och VLA-gränser

Sen kodningsprefix styr nu hela följden av angränsande strängliteraler och
föregående escape-sekvenser. C23:s citerade `#embed` söker arbetskatalogen
efter inkluderarens katalog och före explicita embed-kataloger. Vikbara
arraygränser med sidoeffekter utvärderas som VLA i stället för att tappa anropen.
Fyra oförändrade Kefir-fall passerar alla Clang/vcc-kombinationer på x86;
ARM/QEMU passerar embed och VLA, medan två strängfall avvisas av Clang
på grund av upstreams signed-wchar-antaganden. Portabla strängregressioner
passerar ARM. Unity/separata filer: egna x86 224/224 med sex referensundantag,
ARM 229/229 med ett befintligt kört XFAIL, diagnostik 224/224 och 40 embed-
orakeljämförelser. Kernelns sema 887/887 och full PP-paritet 920/920
passerar både per process och i batch. G1/G2 är fortsatt öppna.
Fryst källkod och verifieringsunderlag: `86c7352`,
`work/results/frontend-milestone-2dc/`.

### Status 2026-11-11: G1, breda heltal till/från flyttal

Linux-konverteringar använder hela heltalsmagnituden med guard/sticky-bitar
och en avrundning till närmaste jämna, respektive avkodning av flyttalets
exponent och signifikand med trunkering mot noll. Ingen körtids-Clang eller
128-bitars trunkering av heltalsindata används. Dynamiska avrundningslägen
ingår inte; detta slutför inte G1.

På den privata milstolpen `7a1691f` passerar 6871 exakta x86-konverteringsprov
och 5667 ARM-prov över fem heltalsbredder 129–7001, med sex respektive fem
flyttalsformat. Unity och separata filer ger samma resultat. Clang-avvikelser
för BF16, x87 och unsigned binary128 registreras separat; vcc måste alltid
matcha den oberoende Python-orakeln. Oförändrat Kefir `bitint_cast_fix2`
passerar fyra x86-kombinationer och två ARM-kombinationer med vcc-kallee;
Clang stöder inte ARM-typens bredd. Snapshot/replay passerar med ASan/UBSan
efter en separat rättning av ett befintligt nollängds-`memcpy` med nullkälla.
Verifieringsunderlag: `work/results/bitint-float-reviewed/`.

Återintegrationen på `b0b6107` bevarar de nyare konstant- och aliasoptimeringarna.
Unity och separata filer passerar samma IEEE-matris, 222/222 egna x86-prov
(med sex befintliga referensundantag), 227/227 ARM-prov (ett befintligt xfail,
inga hoppade prov) och kärnans sema
887/887 både individuellt och i batch. IEEE-matrisen passerar även med `-O3`
på båda målen. Reglerna O0–O3, constant-control, leaf-frame, slot-values,
slot-observable, switch-plan och debugrader passerar i båda byggsätten.
ASan/UBSan passerar IEEE, konstantprov och snapshot/replay på båda målen.
CI kör nu flyttalsmatrisen och sanitizer-replay. Evidens:
`work/results/bitint-float-main-reviewed/`.

Importen ovanpå PR #60 behåller dess fulla Linux-baslinjer och ändrar
endast flyttalsuppföljningen. Båda oförändrade x86-Kefir-fallen
`bitint_cast_fix2` och `bitint_typeconv6` är nu 2/2 godkända med fyra
kompilatorkombinationer vardera; motsvarande standard-ARM-körning
redovisar två Clang-referensavvisningar. Det breda ARM-värdebeviset
kommer från IEEE-matrisen och de separat redovisade vcc-kalleeanropen.
G1/G2 och strikt flyttalsmiljö är fortsatt öppna.

Fulla Kefir-omkörningar på den frysta stacken `a41361e` ger 585 godkända
x86-fall och 461 ARM/QEMU-fall, utan vcc-kompileringsfel. Sex respektive fyra
runtime-/tidsfel och samtliga referens-/unsupported-kategorier behålls;
G1/G2 är fortsatt öppna. Linux-ASan/UBSan går 232/232 x86 och 237/237 ARM
med befintliga undantag. Wasm64-preflight går 189/189 med 38 körda XFAIL;
ExtInt-provet täcker den redan verifierade BitInt-ABI-luckan på Wasm32/64.
Exakta kommandon, oförändrad kompilatorkod och testmarkörens föräldraprov:
`work/results/native-stack64-ci-preflight/`.

G7: ett profilstyrt emittersteg minskar upprepad identifierar- och x86-register-
matchning. Den fasta instruktionsgrinden och de riktade proverna finns i
PLAN-fas2.md; faktisk kompileringstid på hela projekt återstår att mäta.

OPT1-utökningen med tvåpotensmultiplikation och bitkomplement är implementerad
för x86, arm64 och wasm och har fokuserad O0–O3-validering och en liten
matchad kompilatorkostnadsprofil i PLAN-fas2.md. G5/G7 och generell O2-täckning
är fortsatt öppna.

Begränsade switch-värdetabeller har validerats på fyra mål; syntetiska
kompilatorkostnader och deras begränsningar finns i PLAN-fas2.md. G5/G7 och
en realistisk sammansatt kostnadsgrind återstår.

Avgränsade heltalsjämförelser vid typgränser är implementerade för alla fyra
mål och har fokuserade korrekthets- och kompilatorkostnadsprov i PLAN-fas2.md.
G5/G7 och generell O2-täckning är fortsatt öppna.

Den sammansatta optimeringsstacken med värdetabeller klarar den begränsade
1 %-kompilatorkostnadsgrinden på fyra mål (PLAN-fas2.md); den mättade
switchkostnaden och releasegrindarna G5/G7 återstår att följa upp.

Switch-värdetabellernas byteutmatning har snabbats upp efter `perf`-profilering;
matchade mätningar och deras begränsningar finns i PLAN-fas2.md. G5/G7 kvarstår.

Aritmetik- och switchstacken som tidigare hamnade i featuregrenar integreras
nu på main. Fyra måls fokuserade tester, native-sviter och aktuell matchad
kompilatorinstruktionsgrind redovisas i PLAN-fas2.md. G5/G7 återstår.

Typgränser skrivna som begränsade rena konstantuttryck förenklas nu på de
fyra målen. Tester och den avgränsade kompilatorinstruktionsmätningen finns i
PLAN-fas2.md; G5/G7 och generell O2-täckning är fortsatt öppna.

Wasm sänker heltalslikhet med noll och osignerad `x <= 0` till en direkt
nolltestinstruktion. Fyrmålstester och avgränsad kompilatorkostnad finns i
PLAN-fas2.md; G5/G7 och generell O2-täckning är fortsatt öppna.

Avgränsade identiteter för samma ordinära lokala heltalsvariabel förenklas
på alla mål. Fyrmålstester och kompilatorinstruktionsmätning finns i
PLAN-fas2.md; G5/G7 och generell O2-täckning är fortsatt öppna.

Även självjämförelser av ordinära lokala heltal hanteras av det gemensamma
identitetsreceptet, med NaN-regression och fyrmålsmätning i PLAN-fas2.md.
