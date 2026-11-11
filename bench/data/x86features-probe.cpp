#include <llvm/TargetParser/X86TargetParser.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
using namespace llvm;
int main(){
 SmallVector<StringRef> cpus;X86::fillValidCPUArchList(cpus,true);
 for(auto c:cpus){ SmallVector<StringRef> fs;X86::getFeaturesForCPU(c,fs);outs()<<"CPU "<<c;for(auto f:fs)outs()<<" "<<f;outs()<<"\n"; }
 const char *features[]={
#define X86_FEATURE(ENUM,STR) STR,
#define X86_FEATURE_COMPAT(ENUM,STR,ABI,PRIO) STR,
#include <llvm/TargetParser/X86TargetParser.def>
 };
 for(auto f:features){for(bool en:{false,true}){StringMap<bool> fs;X86::updateImpliedFeatures(f,en,fs);outs()<<"FEATURE "<<f<<" "<<en;for(auto &v:fs)outs()<<" "<<v.getKey()<<"="<<v.getValue();outs()<<"\n";}}
}
