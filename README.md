# amigaport

`amigaport` is an embeddable, title-neutral 68000 execution boundary for native Amiga game ports.
It owns architectural CPU state, runtime image identity, bounded execution, and image-qualified native
overrides. Titles inject memory and device behavior; the library never owns a generic emulator UI or
game files. PUAE diagnostics are routed through the caller's configured `Logger`; the fork API never
writes directly to process streams, and contexts without a sink expose a saturating dropped-message
count.

The maintained `SomeoneIsWorking/libretro-uae` fork exports an embeddable CMake target and a C API
for isolated 68000 contexts. The runtime expands all 45,815 legal 68000 encodings through PUAE's
1,540 canonical prefetch handlers and proves live guest fetch, memory callbacks, branches, cycle and
prefetch state, basic exception and interrupt entry, nested contexts, image replacement, native
overrides, and scoped-original dispatch. Exact 68000 bus/address-error frames and trace conformance,
Benefactor integration, and gameplay qualification remain missing; see `docs/project-state.md`.

## Build and verify

```sh
git submodule update --init --recursive
uv run --frozen python tools/verify.py
```

The canonical verifier also owns the hosted Linux x86-64/Clang, Windows x86-64/clang-cl, and Apple
Silicon macOS/AppleClang jobs. macOS linting uses Homebrew clang-tidy with the product compiler's
SDK and libc++ header search paths. Android CI compiles both x86-64 and arm64-v8a with NDK
28.2.13676358 at API 21, audits both linked artifacts, and runs the same complete synthetic runtime
suite on an API 35 x86-64 emulator. The Android job consumes the pinned `shared/android-port`
contract. It does not claim arm64-v8a runtime verification from that x86-64 emulator.

Linux x86-64 has passed locally and in hosted CI. macOS, Windows, and Android remain
unverified until their repaired hosted jobs pass. An APK install check is inapplicable because this
repository produces an embeddable static library, not an Android application package.

## Dependency and license

The pinned CPU source is `SomeoneIsWorking/libretro-uae` revision
`40270e4a5c96c9195deae1801a76788a1e5bc159`, licensed under GPL-2.0. Linking its CPU handlers makes
the distributed combined work subject to GPL-2.0. The dependency's libretro frontend and Amiga
device emulator are not linked into the runtime library.
