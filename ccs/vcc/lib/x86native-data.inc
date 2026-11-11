/* Generated from LLVM llvm/lib/TargetParser/Host.cpp c47d7823685a.
 * Copyright LLVM contributors. Apache-2.0 WITH LLVM-exception.
 * See x86native-LICENSE.txt. Adapted to C and vcc feature masks by
 * bench/gen-x86native.py; no runtime LLVM dependency.
 * Source SHA256 aa72fa102140de2f8a28b195f7b18bb25b44bbc9d19295c14a5991c212c8af52
 */
static const char *x86_native_intel(unsigned Family, unsigned Model, x86_features features) {
  (void)features;
  const char *CPU = NULL;

  switch (Family) {
  case 0x3:
    CPU = "i386";
    break;
  case 0x4:
    CPU = "i486";
    break;
  case 0x5:
    if (x86_feature_has(features, X86_FEATURE_MMX)) {
      CPU = "pentium-mmx";
      break;
    }
    CPU = "pentium";
    break;
  case 0x6:
    switch (Model) {
    case 0x0f: // Intel Core 2 Duo processor, Intel Core 2 Duo mobile
               // processor, Intel Core 2 Quad processor, Intel Core 2 Quad
               // mobile processor, Intel Core 2 Extreme processor, Intel
               // Pentium Dual-Core processor, Intel Xeon processor, model
               // 0Fh. All processors are manufactured using the 65 nm process.
    case 0x16: // Intel Celeron processor model 16h. All processors are
               // manufactured using the 65 nm process
      CPU = "core2";
      break;
    case 0x17: // Intel Core 2 Extreme processor, Intel Xeon processor, model
               // 17h. All processors are manufactured using the 45 nm process.
               //
               // 45nm: Penryn , Wolfdale, Yorkfield (XE)
    case 0x1d: // Intel Xeon processor MP. All processors are manufactured using
               // the 45 nm process.
      CPU = "penryn";
      break;
    case 0x1a: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 45 nm process.
    case 0x1e: // Intel(R) Core(TM) i7 CPU         870  @ 2.93GHz.
               // As found in a Summer 2010 model iMac.
    case 0x1f:
    case 0x2e:              // Nehalem EX
      CPU = "nehalem";
      break;
    case 0x25: // Intel Core i7, laptop version.
    case 0x2c: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 32 nm process.
    case 0x2f: // Westmere EX
      CPU = "westmere";
      break;
    case 0x2a: // Intel Core i7 processor. All processors are manufactured
               // using the 32 nm process.
    case 0x2d:
      CPU = "sandybridge";
      break;
    case 0x3a:
    case 0x3e:              // Ivy Bridge EP
      CPU = "ivybridge";
      break;

    // Haswell:
    case 0x3c:
    case 0x3f:
    case 0x45:
    case 0x46:
      CPU = "haswell";
      break;

    // Broadwell:
    case 0x3d:
    case 0x47:
    case 0x4f:
    case 0x56:
      CPU = "broadwell";
      break;

    // Skylake:
    case 0x4e:              // Skylake mobile
    case 0x5e:              // Skylake desktop
    case 0x8e:              // Kaby Lake mobile
    case 0x9e:              // Kaby Lake desktop
    case 0xa5:              // Comet Lake-H/S
    case 0xa6:              // Comet Lake-U
      CPU = "skylake";
      break;

    // Rocketlake:
    case 0xa7:
      CPU = "rocketlake";
      break;

    // Skylake Xeon:
    case 0x55:
      if (x86_feature_has(features, X86_FEATURE_AVX512BF16)) {
        CPU = "cooperlake";
      } else if (x86_feature_has(features, X86_FEATURE_AVX512VNNI)) {
        CPU = "cascadelake";
      } else {
        CPU = "skylake-avx512";
      }
      break;

    // Cannonlake:
    case 0x66:
      CPU = "cannonlake";
      break;

    // Icelake:
    case 0x7d:
    case 0x7e:
      CPU = "icelake-client";
      break;

    // Tigerlake:
    case 0x8c:
    case 0x8d:
      CPU = "tigerlake";
      break;

    // Alderlake:
    case 0x97:
    case 0x9a:
      CPU = "alderlake";
      break;

    // Gracemont
    case 0xbe:
      CPU = "gracemont";
      break;

    // Raptorlake:
    case 0xb7:
    case 0xba:
    case 0xbf:
      CPU = "raptorlake";
      break;

    // Meteorlake:
    case 0xaa:
    case 0xac:
      CPU = "meteorlake";
      break;

    // Arrowlake:
    case 0xc5:
    // Arrowlake U:
    case 0xb5:
      CPU = "arrowlake";
      break;

    // Arrowlake S:
    case 0xc6:
      CPU = "arrowlake-s";
      break;

    // Lunarlake:
    case 0xbd:
      CPU = "lunarlake";
      break;

    // Pantherlake:
    case 0xcc:
      CPU = "pantherlake";
      break;

    // Wildcatlake:
    case 0xd5:
      CPU = "wildcatlake";
      break;

    // Graniterapids:
    case 0xad:
      CPU = "graniterapids";
      break;

    // Granite Rapids D:
    case 0xae:
      CPU = "graniterapids-d";
      break;

    // Icelake Xeon:
    case 0x6a:
    case 0x6c:
      CPU = "icelake-server";
      break;

    // Emerald Rapids:
    case 0xcf:
      CPU = "emeraldrapids";
      break;

    // Sapphire Rapids:
    case 0x8f:
      CPU = "sapphirerapids";
      break;

    case 0x1c: // Most 45 nm Intel Atom processors
    case 0x26: // 45 nm Atom Lincroft
    case 0x27: // 32 nm Atom Medfield
    case 0x35: // 32 nm Atom Midview
    case 0x36: // 32 nm Atom Midview
      CPU = "bonnell";
      break;

    // Atom Silvermont codes from the Intel software optimization guide.
    case 0x37:
    case 0x4a:
    case 0x4d:
    case 0x5a:
    case 0x5d:
    case 0x4c: // really airmont
      CPU = "silvermont";
      break;
    // Goldmont:
    case 0x5c: // Apollo Lake
    case 0x5f: // Denverton
      CPU = "goldmont";
      break;
    case 0x7a:
      CPU = "goldmont-plus";
      break;
    case 0x86:
    case 0x8a: // Lakefield
    case 0x96: // Elkhart Lake
    case 0x9c: // Jasper Lake
      CPU = "tremont";
      break;

    // Sierraforest:
    case 0xaf:
      CPU = "sierraforest";
      break;

    // Grandridge:
    case 0xb6:
      CPU = "grandridge";
      break;

    // Clearwaterforest:
    case 0xdd:
      CPU = "clearwaterforest";
      break;

    // Xeon Phi (Knights Landing + Knights Mill):
    case 0x57:
      CPU = "knl";
      break;
    case 0x85:
      CPU = "knm";
      break;

    default: // Unknown family 6 CPU, try to guess.
      // Don't both with Type/Subtype here, they aren't used by the caller.
      // They're used above to keep the code in sync with compiler-rt.
      // TODO detect tigerlake host from model
      if (x86_feature_has(features, X86_FEATURE_AVX512VP2INTERSECT)) {
        CPU = "tigerlake";
      } else if (x86_feature_has(features, X86_FEATURE_AVX512VBMI2)) {
        CPU = "icelake-client";
      } else if (x86_feature_has(features, X86_FEATURE_AVX512VBMI)) {
        CPU = "cannonlake";
      } else if (x86_feature_has(features, X86_FEATURE_AVX512BF16)) {
        CPU = "cooperlake";
      } else if (x86_feature_has(features, X86_FEATURE_AVX512VNNI)) {
        CPU = "cascadelake";
      } else if (x86_feature_has(features, X86_FEATURE_AVX512VL)) {
        CPU = "skylake-avx512";
      } else if (x86_feature_has(features, X86_FEATURE_CLFLUSHOPT)) {
        if (x86_feature_has(features, X86_FEATURE_SHA))
          CPU = "goldmont";
        else
          CPU = "skylake";
      } else if (x86_feature_has(features, X86_FEATURE_ADX)) {
        CPU = "broadwell";
      } else if (x86_feature_has(features, X86_FEATURE_AVX2)) {
        CPU = "haswell";
      } else if (x86_feature_has(features, X86_FEATURE_AVX)) {
        CPU = "sandybridge";
      } else if (x86_feature_has(features, X86_FEATURE_SSE4_2)) {
        if (x86_feature_has(features, X86_FEATURE_MOVBE))
          CPU = "silvermont";
        else
          CPU = "nehalem";
      } else if (x86_feature_has(features, X86_FEATURE_SSE4_1)) {
        CPU = "penryn";
      } else if (x86_feature_has(features, X86_FEATURE_SSSE3)) {
        if (x86_feature_has(features, X86_FEATURE_MOVBE))
          CPU = "bonnell";
        else
          CPU = "core2";
      } else if (x86_feature_has(features, X86_FEATURE_64BIT)) {
        CPU = "core2";
      } else if (x86_feature_has(features, X86_FEATURE_SSE3)) {
        CPU = "yonah";
      } else if (x86_feature_has(features, X86_FEATURE_SSE2)) {
        CPU = "pentium-m";
      } else if (x86_feature_has(features, X86_FEATURE_SSE)) {
        CPU = "pentium3";
      } else if (x86_feature_has(features, X86_FEATURE_MMX)) {
        CPU = "pentium2";
      } else {
        CPU = "pentiumpro";
      }
      break;
    }
    break;
  case 0xf: {
    if (x86_feature_has(features, X86_FEATURE_64BIT)) {
      CPU = "nocona";
      break;
    }
    if (x86_feature_has(features, X86_FEATURE_SSE3)) {
      CPU = "prescott";
      break;
    }
    CPU = "pentium4";
    break;
  }
  case 0x13:
    switch (Model) {
    // Diamond Rapids:
    case 0x01:
      CPU = "diamondrapids";
      break;

    default: // Unknown family 19 CPU.
      break;
    }
    break;
  case 0x12:
    switch (Model) {
    // Novalake:
    case 0x1:
    case 0x3:
      CPU = "novalake";
      break;
    default: // Unknown family 0x12 CPU.
      break;
    }
    break;

  default:
    break; // Unknown.
  }

  return CPU;
}
static const char *x86_native_amd(unsigned Family, unsigned Model, x86_features features) {
  (void)features;
  const char *CPU = NULL;

  switch (Family) {
  case 4:
    CPU = "i486";
    break;
  case 5:
    CPU = "pentium";
    switch (Model) {
    case 6:
    case 7:
      CPU = "k6";
      break;
    case 8:
      CPU = "k6-2";
      break;
    case 9:
    case 13:
      CPU = "k6-3";
      break;
    case 10:
      CPU = "geode";
      break;
    }
    break;
  case 6:
    if (x86_feature_has(features, X86_FEATURE_SSE)) {
      CPU = "athlon-xp";
      break;
    }
    CPU = "athlon";
    break;
  case 15:
    if (x86_feature_has(features, X86_FEATURE_SSE3)) {
      CPU = "k8-sse3";
      break;
    }
    CPU = "k8";
    break;
  case 16:
  case 18:
    CPU = "amdfam10";
    switch (Model) {
    case 2:
      break;
    case 4:
      break;
    case 8:
      break;
    }
    break;
  case 20:
    CPU = "btver1";
    break;
  case 21:
    CPU = "bdver1";
    if (Model >= 0x60 && Model <= 0x7f) {
      CPU = "bdver4";
      break; // 60h-7Fh: Excavator
    }
    if (Model >= 0x30 && Model <= 0x3f) {
      CPU = "bdver3";
      break; // 30h-3Fh: Steamroller
    }
    if ((Model >= 0x10 && Model <= 0x1f) || Model == 0x02) {
      CPU = "bdver2";
      break; // 02h, 10h-1Fh: Piledriver
    }
    if (Model <= 0x0f) {
      break; // 00h-0Fh: Bulldozer
    }
    break;
  case 22:
    CPU = "btver2";
    break;
  case 23:
    CPU = "znver1";
    if ((Model >= 0x30 && Model <= 0x3f) || (Model == 0x47) ||
        (Model >= 0x60 && Model <= 0x67) || (Model >= 0x68 && Model <= 0x6f) ||
        (Model >= 0x70 && Model <= 0x7f) || (Model >= 0x84 && Model <= 0x87) ||
        (Model >= 0x90 && Model <= 0x97) || (Model >= 0x98 && Model <= 0x9f) ||
        (Model >= 0xa0 && Model <= 0xaf)) {
      // Family 17h Models 30h-3Fh (Starship) Zen 2
      // Family 17h Models 47h (Cardinal) Zen 2
      // Family 17h Models 60h-67h (Renoir) Zen 2
      // Family 17h Models 68h-6Fh (Lucienne) Zen 2
      // Family 17h Models 70h-7Fh (Matisse) Zen 2
      // Family 17h Models 84h-87h (ProjectX) Zen 2
      // Family 17h Models 90h-97h (VanGogh) Zen 2
      // Family 17h Models 98h-9Fh (Mero) Zen 2
      // Family 17h Models A0h-AFh (Mendocino) Zen 2
      CPU = "znver2";
      break;
    }
    if ((Model >= 0x10 && Model <= 0x1f) || (Model >= 0x20 && Model <= 0x2f)) {
      // Family 17h Models 10h-1Fh (Raven1) Zen
      // Family 17h Models 10h-1Fh (Picasso) Zen+
      // Family 17h Models 20h-2Fh (Raven2 x86) Zen
      break;
    }
    break;
  case 25:
    CPU = "znver3";
    if (Model <= 0x0f || (Model >= 0x20 && Model <= 0x2f) ||
        (Model >= 0x30 && Model <= 0x3f) || (Model >= 0x40 && Model <= 0x4f) ||
        (Model >= 0x50 && Model <= 0x5f)) {
      // Family 19h Models 00h-0Fh (Genesis, Chagall) Zen 3
      // Family 19h Models 20h-2Fh (Vermeer) Zen 3
      // Family 19h Models 30h-3Fh (Badami) Zen 3
      // Family 19h Models 40h-4Fh (Rembrandt) Zen 3+
      // Family 19h Models 50h-5Fh (Cezanne) Zen 3
      break;
    }
    if ((Model >= 0x10 && Model <= 0x1f) || (Model >= 0x60 && Model <= 0x6f) ||
        (Model >= 0x70 && Model <= 0x77) || (Model >= 0x78 && Model <= 0x7f) ||
        (Model >= 0xa0 && Model <= 0xaf)) {
      // Family 19h Models 10h-1Fh (Stones; Storm Peak) Zen 4
      // Family 19h Models 60h-6Fh (Raphael) Zen 4
      // Family 19h Models 70h-77h (Phoenix, Hawkpoint1) Zen 4
      // Family 19h Models 78h-7Fh (Phoenix 2, Hawkpoint2) Zen 4
      // Family 19h Models A0h-AFh (Stones-Dense) Zen 4
      CPU = "znver4";
      break; //  "znver4"
    }
    break; // family 19h
  case 26:
    CPU = "znver5";
    if (Model <= 0x4f || (Model >= 0x60 && Model <= 0x77) ||
        (Model >= 0xd0 && Model <= 0xd7)) {
      // Models 00h-0Fh (Breithorn).
      // Models 10h-1Fh (Breithorn-Dense).
      // Models 20h-2Fh (Strix 1).
      // Models 30h-37h (Strix 2).
      // Models 38h-3Fh (Strix 3).
      // Models 40h-4Fh (Granite Ridge).
      // Models 60h-6Fh (Krackan1).
      // Models 70h-77h (Sarlak).
      // Models D0h-D7h (Annapurna).
      CPU = "znver5";
      break; //  "znver5"
    }
    if ((Model >= 0x50 && Model <= 0x5f) || (Model >= 0x80 && Model <= 0xcf) ||
        (Model >= 0xd8 && Model <= 0xe7)) {
      CPU = "znver6";
      break; //  "znver6"
    }
    break;

  default:
    break; // Unknown AMD CPU.
  }

  return CPU;
}
static const char *x86_native_hygon(unsigned Family, unsigned Model, x86_features features) {
  (void)features;
  const char *CPU = NULL;

  switch (Family) {
  case 24:
    switch (Model) {
    case 4:
      CPU = "c86-4g-m4";
      break; // c86-4g-m4
    case 6:
      CPU = "c86-4g-m6";
      break; // c86-4g-m6
    case 7:
      CPU = "c86-4g-m7";
      break; // c86-4g-m7
    case 8:
      CPU = "c86-4g-m8";
      break; // c86-4g-m8
    }
    break; // Hygon Family 18H
  default:
    break; // Unknown Hygon CPU.
  }

  return CPU;
}
static x86_features x86_native_name_features(const x86_native_io *io, unsigned ECX, unsigned EDX, unsigned MaxLeaf) {
  x86_features features = {{0}};
  unsigned EAX = 0, EBX;



  if ((EDX >> 15) & 1)
    features.bits[X86_FEATURE_CMOV / 64] |= UINT64_C(1) << (X86_FEATURE_CMOV % 64);
  if ((EDX >> 23) & 1)
    features.bits[X86_FEATURE_MMX / 64] |= UINT64_C(1) << (X86_FEATURE_MMX % 64);
  if ((EDX >> 25) & 1)
    features.bits[X86_FEATURE_SSE / 64] |= UINT64_C(1) << (X86_FEATURE_SSE % 64);
  if ((EDX >> 26) & 1)
    features.bits[X86_FEATURE_SSE2 / 64] |= UINT64_C(1) << (X86_FEATURE_SSE2 % 64);

  if ((ECX >> 0) & 1)
    features.bits[X86_FEATURE_SSE3 / 64] |= UINT64_C(1) << (X86_FEATURE_SSE3 % 64);
  if ((ECX >> 1) & 1)
    features.bits[X86_FEATURE_PCLMUL / 64] |= UINT64_C(1) << (X86_FEATURE_PCLMUL % 64);
  if ((ECX >> 9) & 1)
    features.bits[X86_FEATURE_SSSE3 / 64] |= UINT64_C(1) << (X86_FEATURE_SSSE3 % 64);
  if ((ECX >> 12) & 1)
    features.bits[X86_FEATURE_FMA / 64] |= UINT64_C(1) << (X86_FEATURE_FMA % 64);
  if ((ECX >> 19) & 1)
    features.bits[X86_FEATURE_SSE4_1 / 64] |= UINT64_C(1) << (X86_FEATURE_SSE4_1 % 64);
  if ((ECX >> 20) & 1) {
    features.bits[X86_FEATURE_SSE4_2 / 64] |= UINT64_C(1) << (X86_FEATURE_SSE4_2 % 64);
    features.bits[X86_FEATURE_CRC32 / 64] |= UINT64_C(1) << (X86_FEATURE_CRC32 % 64);
  }
  if ((ECX >> 23) & 1)
    features.bits[X86_FEATURE_POPCNT / 64] |= UINT64_C(1) << (X86_FEATURE_POPCNT % 64);
  if ((ECX >> 25) & 1)
    features.bits[X86_FEATURE_AES / 64] |= UINT64_C(1) << (X86_FEATURE_AES % 64);

  if ((ECX >> 22) & 1)
    features.bits[X86_FEATURE_MOVBE / 64] |= UINT64_C(1) << (X86_FEATURE_MOVBE % 64);

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  const unsigned AVXBits = (1 << 27) | (1 << 28);
  bool HasAVX = ((ECX & AVXBits) == AVXBits) && !x86_native_xcr0(io, &EAX, &EDX) &&
                ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVX && ((EAX & 0xe0) == 0xe0);
#endif

  if (HasAVX)
    features.bits[X86_FEATURE_AVX / 64] |= UINT64_C(1) << (X86_FEATURE_AVX % 64);

  bool HasLeaf7 =
      MaxLeaf >= 0x7 && !x86_native_cpuid(io, 0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  if (HasLeaf7 && ((EBX >> 3) & 1))
    features.bits[X86_FEATURE_BMI / 64] |= UINT64_C(1) << (X86_FEATURE_BMI % 64);
  if (HasLeaf7 && ((EBX >> 5) & 1) && HasAVX)
    features.bits[X86_FEATURE_AVX2 / 64] |= UINT64_C(1) << (X86_FEATURE_AVX2 % 64);
  if (HasLeaf7 && ((EBX >> 8) & 1))
    features.bits[X86_FEATURE_BMI2 / 64] |= UINT64_C(1) << (X86_FEATURE_BMI2 % 64);
  if (HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save) {
    features.bits[X86_FEATURE_AVX512F / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512F % 64);
  }
  if (HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512DQ / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512DQ % 64);
  if (HasLeaf7 && ((EBX >> 19) & 1))
    features.bits[X86_FEATURE_ADX / 64] |= UINT64_C(1) << (X86_FEATURE_ADX % 64);
  if (HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512IFMA / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512IFMA % 64);
  if (HasLeaf7 && ((EBX >> 23) & 1))
    features.bits[X86_FEATURE_CLFLUSHOPT / 64] |= UINT64_C(1) << (X86_FEATURE_CLFLUSHOPT % 64);
  if (HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512CD / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512CD % 64);
  if (HasLeaf7 && ((EBX >> 29) & 1))
    features.bits[X86_FEATURE_SHA / 64] |= UINT64_C(1) << (X86_FEATURE_SHA % 64);
  if (HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512BW / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512BW % 64);
  if (HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512VL / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512VL % 64);

  if (HasLeaf7 && ((ECX >> 1) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512VBMI / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512VBMI % 64);
  if (HasLeaf7 && ((ECX >> 6) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512VBMI2 / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512VBMI2 % 64);
  if (HasLeaf7 && ((ECX >> 8) & 1))
    features.bits[X86_FEATURE_GFNI / 64] |= UINT64_C(1) << (X86_FEATURE_GFNI % 64);
  if (HasLeaf7 && ((ECX >> 10) & 1) && HasAVX)
    features.bits[X86_FEATURE_VPCLMULQDQ / 64] |= UINT64_C(1) << (X86_FEATURE_VPCLMULQDQ % 64);
  if (HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512VNNI / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512VNNI % 64);
  if (HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512BITALG / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512BITALG % 64);
  if (HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512VPOPCNTDQ / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512VPOPCNTDQ % 64);

  if (HasLeaf7 && ((EDX >> 2) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX5124VNNIW / 64] |= UINT64_C(1) << (X86_FEATURE_AVX5124VNNIW % 64);
  if (HasLeaf7 && ((EDX >> 3) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX5124FMAPS / 64] |= UINT64_C(1) << (X86_FEATURE_AVX5124FMAPS % 64);
  if (HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512VP2INTERSECT / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512VP2INTERSECT % 64);

  // EAX from subleaf 0 is the maximum subleaf supported. Some CPUs don't
  // return all 0s for invalid subleaves so check the limit.
  bool HasLeaf7Subleaf1 =
      HasLeaf7 && EAX >= 1 &&
      !x86_native_cpuid(io, 0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save)
    features.bits[X86_FEATURE_AVX512BF16 / 64] |= UINT64_C(1) << (X86_FEATURE_AVX512BF16 % 64);

  unsigned MaxExtLevel;
  x86_native_cpuid0(io, 0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !x86_native_cpuid0(io, 0x80000001, &EAX, &EBX, &ECX, &EDX);
  if (HasExtLeaf1 && ((ECX >> 6) & 1))
    features.bits[X86_FEATURE_SSE4A / 64] |= UINT64_C(1) << (X86_FEATURE_SSE4A % 64);
  if (HasExtLeaf1 && ((ECX >> 11) & 1))
    features.bits[X86_FEATURE_XOP / 64] |= UINT64_C(1) << (X86_FEATURE_XOP % 64);
  if (HasExtLeaf1 && ((ECX >> 16) & 1))
    features.bits[X86_FEATURE_FMA4 / 64] |= UINT64_C(1) << (X86_FEATURE_FMA4 % 64);

  if (HasExtLeaf1 && ((EDX >> 29) & 1))
    features.bits[X86_FEATURE_64BIT / 64] |= UINT64_C(1) << (X86_FEATURE_64BIT % 64);
  return features;
}
static bool x86_native_values(const x86_native_io *io, bool values[X86_FEATURE_COUNT]) {
  unsigned EAX = 0, EBX = 0, ECX = 0, EDX = 0;
  unsigned MaxLevel;

  if (x86_native_cpuid0(io, 0, &MaxLevel, &EBX, &ECX, &EDX) || MaxLevel < 1)
    return false;

  x86_native_cpuid0(io, 1, &EAX, &EBX, &ECX, &EDX);

  values[X86_FEATURE_CX8]    = (EDX >>  8) & 1;
  values[X86_FEATURE_CMOV]   = (EDX >> 15) & 1;
  values[X86_FEATURE_MMX]    = (EDX >> 23) & 1;
  values[X86_FEATURE_FXSR]   = (EDX >> 24) & 1;
  values[X86_FEATURE_SSE]    = (EDX >> 25) & 1;
  values[X86_FEATURE_SSE2]   = (EDX >> 26) & 1;

  values[X86_FEATURE_SSE3]   = (ECX >>  0) & 1;
  values[X86_FEATURE_PCLMUL] = (ECX >>  1) & 1;
  values[X86_FEATURE_SSSE3]  = (ECX >>  9) & 1;
  values[X86_FEATURE_CX16]   = (ECX >> 13) & 1;
  values[X86_FEATURE_SSE4_1] = (ECX >> 19) & 1;
  values[X86_FEATURE_SSE4_2] = (ECX >> 20) & 1;
  values[X86_FEATURE_CRC32]  = values[X86_FEATURE_SSE4_2];
  values[X86_FEATURE_MOVBE]  = (ECX >> 22) & 1;
  values[X86_FEATURE_POPCNT] = (ECX >> 23) & 1;
  values[X86_FEATURE_AES]    = (ECX >> 25) & 1;
  values[X86_FEATURE_RDRND]  = (ECX >> 30) & 1;

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  bool HasXSave = ((ECX >> 27) & 1) && !x86_native_xcr0(io, &EAX, &EDX);
  bool HasAVXSave = HasXSave && ((ECX >> 28) & 1) && ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVXSave && ((EAX & 0xe0) == 0xe0);
#endif
  // AMX requires additional context to be saved by the OS.
  const unsigned AMXBits = (1 << 17) | (1 << 18);
  bool HasAMXSave = HasXSave && ((EAX & AMXBits) == AMXBits);
  // APX requires additional context to be saved by the OS.
  bool HasAPXSave = HasXSave && ((EAX >> 19) & 1);

  values[X86_FEATURE_AVX]   = HasAVXSave;
  values[X86_FEATURE_FMA]   = ((ECX >> 12) & 1) && HasAVXSave;
  // Only enable XSAVE if OS has enabled support for saving YMM state.
  values[X86_FEATURE_XSAVE] = ((ECX >> 26) & 1) && HasAVXSave;
  values[X86_FEATURE_F16C]  = ((ECX >> 29) & 1) && HasAVXSave;

  unsigned MaxExtLevel;
  x86_native_cpuid0(io, 0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !x86_native_cpuid0(io, 0x80000001, &EAX, &EBX, &ECX, &EDX);
  values[X86_FEATURE_SAHF]   = HasExtLeaf1 && ((ECX >>  0) & 1);
  values[X86_FEATURE_LZCNT]  = HasExtLeaf1 && ((ECX >>  5) & 1);
  values[X86_FEATURE_SSE4A]  = HasExtLeaf1 && ((ECX >>  6) & 1);
  values[X86_FEATURE_PRFCHW] = HasExtLeaf1 && ((ECX >>  8) & 1);
  values[X86_FEATURE_XOP]    = HasExtLeaf1 && ((ECX >> 11) & 1) && HasAVXSave;
  values[X86_FEATURE_LWP]    = HasExtLeaf1 && ((ECX >> 15) & 1);
  values[X86_FEATURE_FMA4]   = HasExtLeaf1 && ((ECX >> 16) & 1) && HasAVXSave;
  values[X86_FEATURE_TBM]    = HasExtLeaf1 && ((ECX >> 21) & 1);
  values[X86_FEATURE_MWAITX] = HasExtLeaf1 && ((ECX >> 29) & 1);

  values[X86_FEATURE_64BIT]  = HasExtLeaf1 && ((EDX >> 29) & 1);

  // Miscellaneous memory related features, detected by
  // using the 0x80000008 leaf of the CPUID instruction
  bool HasExtLeaf8 = MaxExtLevel >= 0x80000008 &&
                     !x86_native_cpuid0(io, 0x80000008, &EAX, &EBX, &ECX, &EDX);
  values[X86_FEATURE_CLZERO]   = HasExtLeaf8 && ((EBX >> 0) & 1);
  values[X86_FEATURE_RDPRU]    = HasExtLeaf8 && ((EBX >> 4) & 1);
  values[X86_FEATURE_WBNOINVD] = HasExtLeaf8 && ((EBX >> 9) & 1);

  bool HasExtLeaf21 = MaxExtLevel >= 0x80000021 &&
                      !x86_native_cpuid0(io, 0x80000021, &EAX, &EBX, &ECX, &EDX);
  // AMD cpuid bit for prefetchi is different from Intel
  values[X86_FEATURE_PREFETCHI] = HasExtLeaf21 && ((EAX >> 20) & 1);
  values[X86_FEATURE_AVX512BMM] = HasExtLeaf21 && ((EAX >> 23) & 1) && HasAVX512Save;

  bool HasLeaf7 =
      MaxLevel >= 7 && !x86_native_cpuid(io, 0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  values[X86_FEATURE_FSGSBASE]   = HasLeaf7 && ((EBX >>  0) & 1);
  values[X86_FEATURE_SGX]        = HasLeaf7 && ((EBX >>  2) & 1);
  values[X86_FEATURE_BMI]        = HasLeaf7 && ((EBX >>  3) & 1);
  // AVX2 is only supported if we have the OS save support from AVX.
  values[X86_FEATURE_AVX2]       = HasLeaf7 && ((EBX >>  5) & 1) && HasAVXSave;
  values[X86_FEATURE_BMI2]       = HasLeaf7 && ((EBX >>  8) & 1);
  values[X86_FEATURE_INVPCID]    = HasLeaf7 && ((EBX >> 10) & 1);
  values[X86_FEATURE_RTM]        = HasLeaf7 && ((EBX >> 11) & 1);
  // AVX512 is only supported if the OS supports the context save for it.
  values[X86_FEATURE_AVX512F]    = HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save;
  values[X86_FEATURE_AVX512DQ]   = HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save;
  values[X86_FEATURE_RDSEED]     = HasLeaf7 && ((EBX >> 18) & 1);
  values[X86_FEATURE_ADX]        = HasLeaf7 && ((EBX >> 19) & 1);
  values[X86_FEATURE_AVX512IFMA] = HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save;
  values[X86_FEATURE_CLFLUSHOPT] = HasLeaf7 && ((EBX >> 23) & 1);
  values[X86_FEATURE_CLWB]       = HasLeaf7 && ((EBX >> 24) & 1);
  values[X86_FEATURE_AVX512CD]   = HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save;
  values[X86_FEATURE_SHA]        = HasLeaf7 && ((EBX >> 29) & 1);
  values[X86_FEATURE_AVX512BW]   = HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save;
  values[X86_FEATURE_AVX512VL]   = HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save;

  values[X86_FEATURE_AVX512VBMI]      = HasLeaf7 && ((ECX >>  1) & 1) && HasAVX512Save;
  values[X86_FEATURE_PKU]             = HasLeaf7 && ((ECX >>  4) & 1);
  values[X86_FEATURE_WAITPKG]         = HasLeaf7 && ((ECX >>  5) & 1);
  values[X86_FEATURE_AVX512VBMI2]     = HasLeaf7 && ((ECX >>  6) & 1) && HasAVX512Save;
  values[X86_FEATURE_SHSTK]           = HasLeaf7 && ((ECX >>  7) & 1);
  values[X86_FEATURE_GFNI]            = HasLeaf7 && ((ECX >>  8) & 1);
  values[X86_FEATURE_VAES]            = HasLeaf7 && ((ECX >>  9) & 1) && HasAVXSave;
  values[X86_FEATURE_VPCLMULQDQ]      = HasLeaf7 && ((ECX >> 10) & 1) && HasAVXSave;
  values[X86_FEATURE_AVX512VNNI]      = HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save;
  values[X86_FEATURE_AVX512BITALG]    = HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save;
  values[X86_FEATURE_AVX512VPOPCNTDQ] = HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save;
  values[X86_FEATURE_RDPID]           = HasLeaf7 && ((ECX >> 22) & 1);
  values[X86_FEATURE_KL]              = HasLeaf7 && ((ECX >> 23) & 1); // key locker
  values[X86_FEATURE_CLDEMOTE]        = HasLeaf7 && ((ECX >> 25) & 1);
  values[X86_FEATURE_MOVDIRI]         = HasLeaf7 && ((ECX >> 27) & 1);
  values[X86_FEATURE_MOVDIR64B]       = HasLeaf7 && ((ECX >> 28) & 1);
  values[X86_FEATURE_ENQCMD]          = HasLeaf7 && ((ECX >> 29) & 1);

  values[X86_FEATURE_UINTR]           = HasLeaf7 && ((EDX >> 5) & 1);
  values[X86_FEATURE_AVX512VP2INTERSECT] =
      HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save;
  values[X86_FEATURE_SERIALIZE]       = HasLeaf7 && ((EDX >> 14) & 1);
  values[X86_FEATURE_TSXLDTRK]        = HasLeaf7 && ((EDX >> 16) & 1);
  // There are two CPUID leafs which information associated with the pconfig
  // instruction:
  // EAX=0x7, ECX=0x0 indicates the availability of the instruction (via the 18th
  // bit of EDX), while the EAX=0x1b leaf returns information on the
  // availability of specific pconfig leafs.
  // The target feature here only refers to the the first of these two.
  // Users might need to check for the availability of specific pconfig
  // leaves using cpuid, since that information is ignored while
  // detecting features using the "-march=native" flag.
  // For more info, see X86 ISA docs.
  values[X86_FEATURE_PCONFIG] = HasLeaf7 && ((EDX >> 18) & 1);
  values[X86_FEATURE_AMX_BF16]   = HasLeaf7 && ((EDX >> 22) & 1) && HasAMXSave;
  values[X86_FEATURE_AVX512FP16] = HasLeaf7 && ((EDX >> 23) & 1) && HasAVX512Save;
  values[X86_FEATURE_AMX_TILE]   = HasLeaf7 && ((EDX >> 24) & 1) && HasAMXSave;
  values[X86_FEATURE_AMX_INT8]   = HasLeaf7 && ((EDX >> 25) & 1) && HasAMXSave;
  // EAX from subleaf 0 is the maximum subleaf supported. Some CPUs don't
  // return all 0s for invalid subleaves so check the limit.
  bool HasLeaf7Subleaf1 =
      HasLeaf7 && EAX >= 1 &&
      !x86_native_cpuid(io, 0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  values[X86_FEATURE_SHA512]     = HasLeaf7Subleaf1 && ((EAX >> 0) & 1);
  values[X86_FEATURE_SM3]        = HasLeaf7Subleaf1 && ((EAX >> 1) & 1);
  values[X86_FEATURE_SM4]        = HasLeaf7Subleaf1 && ((EAX >> 2) & 1);
  values[X86_FEATURE_RAOINT]     = HasLeaf7Subleaf1 && ((EAX >> 3) & 1);
  values[X86_FEATURE_AVXVNNI]    = HasLeaf7Subleaf1 && ((EAX >> 4) & 1) && HasAVXSave;
  values[X86_FEATURE_AVX512BF16] = HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save;
  values[X86_FEATURE_AMX_FP16]   = HasLeaf7Subleaf1 && ((EAX >> 21) & 1) && HasAMXSave;
  values[X86_FEATURE_CMPCCXADD]  = HasLeaf7Subleaf1 && ((EAX >> 7) & 1);
  values[X86_FEATURE_HRESET]     = HasLeaf7Subleaf1 && ((EAX >> 22) & 1);
  values[X86_FEATURE_AVXIFMA]    = HasLeaf7Subleaf1 && ((EAX >> 23) & 1) && HasAVXSave;
  values[X86_FEATURE_MOVRS] = HasLeaf7Subleaf1 && ((EAX >> 31) & 1);
  values[X86_FEATURE_AVXVNNIINT8] = HasLeaf7Subleaf1 && ((EDX >> 4) & 1) && HasAVXSave;
  values[X86_FEATURE_AVXNECONVERT] = HasLeaf7Subleaf1 && ((EDX >> 5) & 1) && HasAVXSave;
  values[X86_FEATURE_AMX_COMPLEX] = HasLeaf7Subleaf1 && ((EDX >> 8) & 1) && HasAMXSave;
  values[X86_FEATURE_AVXVNNIINT16] = HasLeaf7Subleaf1 && ((EDX >> 10) & 1) && HasAVXSave;
  values[X86_FEATURE_PREFETCHI] |= HasLeaf7Subleaf1 && ((EDX >> 14) & 1);
  values[X86_FEATURE_USERMSR]  = HasLeaf7Subleaf1 && ((EDX >> 15) & 1);
  bool HasAVX10 = HasLeaf7Subleaf1 && ((EDX >> 19) & 1);
  bool HasAPXF = HasLeaf7Subleaf1 && ((EDX >> 21) & 1) && HasAPXSave;
  values[X86_FEATURE_EGPR] = HasAPXF;
  // TODO: We may need to check OS or MSVC version once unwinder opcodes
  // support PUSH2/POP2/PPX.
  values[X86_FEATURE_PUSH2POP2] = HasAPXF;
  values[X86_FEATURE_PPX] = HasAPXF;
  values[X86_FEATURE_NDD] = HasAPXF;
  values[X86_FEATURE_CCMP] = HasAPXF;
  values[X86_FEATURE_NF] = HasAPXF;
  values[X86_FEATURE_CF] = HasAPXF;
  values[X86_FEATURE_ZU] = HasAPXF;
  values[X86_FEATURE_JMPABS] = HasAPXF;

  bool HasLeafD = MaxLevel >= 0xd &&
                  !x86_native_cpuid(io, 0xd, 0x1, &EAX, &EBX, &ECX, &EDX);

  // Only enable XSAVE if OS has enabled support for saving YMM state.
  values[X86_FEATURE_XSAVEOPT] = HasLeafD && ((EAX >> 0) & 1) && HasAVXSave;
  values[X86_FEATURE_XSAVEC]   = HasLeafD && ((EAX >> 1) & 1) && HasAVXSave;
  values[X86_FEATURE_XSAVES]   = HasLeafD && ((EAX >> 3) & 1) && HasAVXSave;

  bool HasLeaf14 = MaxLevel >= 0x14 &&
                  !x86_native_cpuid(io, 0x14, 0x0, &EAX, &EBX, &ECX, &EDX);

  values[X86_FEATURE_PTWRITE] = HasLeaf14 && ((EBX >> 4) & 1);

  bool HasLeaf19 =
      MaxLevel >= 0x19 && !x86_native_cpuid0(io, 0x19, &EAX, &EBX, &ECX, &EDX);
  values[X86_FEATURE_WIDEKL] = HasLeaf7 && HasLeaf19 && ((EBX >> 2) & 1);

  bool HasLeaf1E = MaxLevel >= 0x1e &&
                   !x86_native_cpuid(io, 0x1e, 0x1, &EAX, &EBX, &ECX, &EDX);
  values[X86_FEATURE_AMX_FP8] = HasLeaf1E && ((EAX >> 4) & 1) && HasAMXSave;
  values[X86_FEATURE_AMX_AVX512] = HasLeaf1E && ((EAX >> 7) & 1) && HasAMXSave;
  values[X86_FEATURE_AMX_MOVRS] = HasLeaf1E && ((EAX >> 8) & 1) && HasAMXSave;

  bool HasLeaf24 = MaxLevel >= 0x24 &&
                   !x86_native_cpuid(io, 0x24, 0x0, &EAX, &EBX, &ECX, &EDX);

  int AVX10Ver = HasLeaf24 ? (EBX & 0xff) : 0;
  values[X86_FEATURE_AVX10_1] = HasAVX10 && AVX10Ver >= 1;
  values[X86_FEATURE_AVX10_2] = HasAVX10 && AVX10Ver >= 2;

  return true;
}
static const unsigned x86_native_known[] = {
X86_FEATURE_64BIT,X86_FEATURE_ADX,X86_FEATURE_AES,X86_FEATURE_AMX_AVX512,X86_FEATURE_AMX_BF16,X86_FEATURE_AMX_COMPLEX,X86_FEATURE_AMX_FP16,X86_FEATURE_AMX_FP8,X86_FEATURE_AMX_INT8,X86_FEATURE_AMX_MOVRS,X86_FEATURE_AMX_TILE,X86_FEATURE_AVX,X86_FEATURE_AVX10_1,X86_FEATURE_AVX10_2,X86_FEATURE_AVX2,X86_FEATURE_AVX512BF16,X86_FEATURE_AVX512BITALG,X86_FEATURE_AVX512BMM,X86_FEATURE_AVX512BW,X86_FEATURE_AVX512CD,X86_FEATURE_AVX512DQ,X86_FEATURE_AVX512F,X86_FEATURE_AVX512FP16,X86_FEATURE_AVX512IFMA,X86_FEATURE_AVX512VBMI,X86_FEATURE_AVX512VBMI2,X86_FEATURE_AVX512VL,X86_FEATURE_AVX512VNNI,X86_FEATURE_AVX512VP2INTERSECT,X86_FEATURE_AVX512VPOPCNTDQ,X86_FEATURE_AVXIFMA,X86_FEATURE_AVXNECONVERT,X86_FEATURE_AVXVNNI,X86_FEATURE_AVXVNNIINT16,X86_FEATURE_AVXVNNIINT8,X86_FEATURE_BMI,X86_FEATURE_BMI2,X86_FEATURE_CCMP,X86_FEATURE_CF,X86_FEATURE_CLDEMOTE,X86_FEATURE_CLFLUSHOPT,X86_FEATURE_CLWB,X86_FEATURE_CLZERO,X86_FEATURE_CMOV,X86_FEATURE_CMPCCXADD,X86_FEATURE_CRC32,X86_FEATURE_CX16,X86_FEATURE_CX8,X86_FEATURE_EGPR,X86_FEATURE_ENQCMD,X86_FEATURE_F16C,X86_FEATURE_FMA,X86_FEATURE_FMA4,X86_FEATURE_FSGSBASE,X86_FEATURE_FXSR,X86_FEATURE_GFNI,X86_FEATURE_HRESET,X86_FEATURE_INVPCID,X86_FEATURE_JMPABS,X86_FEATURE_KL,X86_FEATURE_LWP,X86_FEATURE_LZCNT,X86_FEATURE_MMX,X86_FEATURE_MOVBE,X86_FEATURE_MOVDIR64B,X86_FEATURE_MOVDIRI,X86_FEATURE_MOVRS,X86_FEATURE_MWAITX,X86_FEATURE_NDD,X86_FEATURE_NF,X86_FEATURE_PCLMUL,X86_FEATURE_PCONFIG,X86_FEATURE_PKU,X86_FEATURE_POPCNT,X86_FEATURE_PPX,X86_FEATURE_PREFETCHI,X86_FEATURE_PRFCHW,X86_FEATURE_PTWRITE,X86_FEATURE_PUSH2POP2,X86_FEATURE_RAOINT,X86_FEATURE_RDPID,X86_FEATURE_RDPRU,X86_FEATURE_RDRND,X86_FEATURE_RDSEED,X86_FEATURE_RTM,X86_FEATURE_SAHF,X86_FEATURE_SERIALIZE,X86_FEATURE_SGX,X86_FEATURE_SHA,X86_FEATURE_SHA512,X86_FEATURE_SHSTK,X86_FEATURE_SM3,X86_FEATURE_SM4,X86_FEATURE_SSE,X86_FEATURE_SSE2,X86_FEATURE_SSE3,X86_FEATURE_SSE4_1,X86_FEATURE_SSE4_2,X86_FEATURE_SSE4A,X86_FEATURE_SSSE3,X86_FEATURE_TBM,X86_FEATURE_TSXLDTRK,X86_FEATURE_UINTR,X86_FEATURE_USERMSR,X86_FEATURE_VAES,X86_FEATURE_VPCLMULQDQ,X86_FEATURE_WAITPKG,X86_FEATURE_WBNOINVD,X86_FEATURE_WIDEKL,X86_FEATURE_XOP,X86_FEATURE_XSAVE,X86_FEATURE_XSAVEC,X86_FEATURE_XSAVEOPT,X86_FEATURE_XSAVES,X86_FEATURE_ZU
};
