#!/usr/bin/env python3
"""Translate the x86 portions of pinned LLVM Host.cpp into standalone C.
Usage: gen-x86native.py /path/to/llvm/lib/TargetParser/Host.cpp
Source revision c47d7823685a, LLVM23.1.6; see x86native-LICENSE.txt.
"""
import hashlib,pathlib,re,sys
root=pathlib.Path(__file__).resolve().parents[1]
s=pathlib.Path(sys.argv[1]).read_text()
def func(start):
 p=s.index(start);b=s.index('{',p);depth=1;e=b+1
 while depth:
  if s[e]=='{':depth+=1
  if s[e]=='}':depth-=1
  e+=1
 return s[p:e]
def calls(t):
 t=t.replace('getX86CpuIDAndInfoEx(', 'x86_native_cpuid(io, ').replace('getX86CpuIDAndInfo(', 'x86_native_cpuid0(io, ').replace('getX86XCR0(', 'x86_native_xcr0(io, ')
 return t
header='''/* Generated from LLVM llvm/lib/TargetParser/Host.cpp c47d7823685a.
 * Copyright LLVM contributors. Apache-2.0 WITH LLVM-exception.
 * See x86native-LICENSE.txt. Adapted to C and vcc feature masks by
 * bench/gen-x86native.py; no runtime LLVM dependency.
'''+f' * Source SHA256 {hashlib.sha256(s.encode()).hexdigest()}\n */\n'
out=header
for vendor,typ in [('Intel','StringRef'),('AMD','const char *'),('Hygon','StringRef')]:
 t=func('static '+typ+(' ' if typ=='StringRef' else '')+'get'+vendor+'ProcessorTypeAndSubtype')
 b=t.index('{');t=f'static const char *x86_native_{vendor.lower()}(unsigned Family, unsigned Model, x86_features features) '+t[b:]
 t=t.replace('{', '{\n  (void)features;', 1)
 t=t.replace('StringRef CPU;', 'const char *CPU = NULL;').replace('nullptr','NULL')
 t=re.sub(r'^[ \t]*\*(?:Type|Subtype) = [^;]+;[^\n]*\n','',t,flags=re.M)
 t=re.sub(r'testFeature\(X86::FEATURE_(\w+)\)',r'x86_feature_has(features, X86_FEATURE_\1)',t)
 t=t.replace('X86_FEATURE_SSE4_A','X86_FEATURE_SSE4A')
 out+=t+'\n'
t=func('static void getAvailableFeatures')
t=t[t.index('{'):];t=re.sub(r'auto setFeature = \[&\]\(unsigned F\) \{.*?\};','',t,flags=re.S)
t=re.sub(r'setFeature\(X86::FEATURE_(\w+)\);',r'features.bits[X86_FEATURE_\1 / 64] |= UINT64_C(1) << (X86_FEATURE_\1 % 64);',t)
t=t.replace('X86_FEATURE_SSE4_A','X86_FEATURE_SSE4A')
t=t.replace('{\n  unsigned EAX, EBX;','{\n  x86_features features = {{0}};\n  unsigned EAX = 0, EBX;')
t=t[:-1]+'  return features;\n}'
out+='static x86_features x86_native_name_features(const x86_native_io *io, unsigned ECX, unsigned EDX, unsigned MaxLeaf) '+calls(t)+'\n'
t=func('StringMap<bool> sys::getHostCPUFeatures()')
t=t[t.index('{'):].replace('  StringMap<bool> Features;\n','').replace('return Features;', 'return false;',1)
known=sorted(set(re.findall(r'Features\["([^"]+)"\]',t)))
t=re.sub(r'Features\["([^"]+)"\]',lambda m:'values[X86_FEATURE_'+re.sub('[^A-Z0-9]','_',m[1].upper())+']',t)
t=t.replace('return Features;','return true;')
out+='static bool x86_native_values(const x86_native_io *io, bool values[X86_FEATURE_COUNT]) '+calls(t)+'\n'
out+='static const unsigned x86_native_known[] = {\n'+','.join('X86_FEATURE_'+re.sub('[^A-Z0-9]','_',f.upper()) for f in known)+'\n};\n'
(root/'src/include/port/x86native-data.inc').write_text('\n'.join(line.rstrip() for line in out.splitlines())+'\n')
print(len(known),'CPUID feature values generated')
