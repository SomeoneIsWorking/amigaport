# Project state

## Comparison baseline

The baseline is embedding PUAE's global libretro frontend or copying an unowned 68000 implementation
into each title. `amigaport` instead exposes a title-neutral per-instance execution contract and pins
the maintained source dependency.

## Current focus

S001 is the current focus.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
| --- | --- | --- | --- | --- |
| S001 | Runtime executes the complete 68000 instruction set through the maintained PUAE CPU owner | partial | — | G001 |
| S002 | Complete D/A/PC/SR, supervisor, interrupt, exception, prefetch, and cycle state has one public owner | partial | S001 | G001 |
| S003 | Image-generation-aware overrides and scoped original calls use the shipping dispatcher | verified | S001 | G001 |
| S004 | Bounded exits preserve precise guest identity, instruction counts, cycles, faults, opcodes, and exceptions | verified | S001 | G001 |
| S005 | Representative gameplay is conformant and performant on Linux x86-64 | missing | S001 | G001 |
| S006 | Representative gameplay is conformant and performant on Apple Silicon macOS | missing | S001 | G001 |
| S007 | Representative gameplay is conformant and performant on Android arm64-v8a | missing | S001 | G001 |
| S008 | Asset-free Linux x86-64 hosted CI builds, lints, audits, and runs the synthetic runtime | verified | S001 | G001 |
| S009 | Asset-free Windows x86-64 hosted CI builds, lints, audits, and runs the synthetic runtime | verified | S001 | G001 |
| S010 | Asset-free Apple Silicon macOS hosted CI builds, lints, audits, and runs the synthetic runtime | verified | S001 | G001 |
| S011 | Asset-free Android CI builds x86-64 and arm64-v8a and runs the synthetic runtime on a matching emulator ABI | partial | S001 | G001 |

### S001 — Maintained CPU execution

The maintained fork now exports `PUAE::M68kEmbed` and an opaque callback-driven C API. It builds the
68000 prefetch table from PUAE's own decoder metadata: 45,815 legal encoded opcodes resolve to all
1,540 canonical table-12 handlers. Illegal, Line-A, and Line-F encodings enter exception vectors;
there is no parent-owned opcode shim or alternative execution selector.

Gap: PUAE's exact 68000 bus/address-error stack-frame builders remain coupled to the full machine
core. Callback faults and odd accesses currently exit precisely before a fabricated frame is
created. RESET is delegated to an optional device callback, but device reset conformance is not yet
proven. These gaps block declaring the complete CPU boundary verified.

Evidence: Linux x86-64 focused tests cover register and memory operations, taken branches, prefetch
identity, basic exception frames, autovector interrupt entry, and nested context execution. The
linked-artifact audit reaches exactly 1,540 table-12 handlers and reports zero generic frontend or
device-owner symbols.

### S002 — Complete state owner

The public context owns all D/A registers, PC, SR, active and shadow stack pointers, interrupt level,
prefetch words plus their address/validity, stopped/halted state, current exception identity, and
monotonic instruction/cycle counters. Tests cover supervisor exception frames, interrupt masks,
IR/IRC movement, and stack consistency in addition to ordinary instructions. The fork's actual
`write_log` hook is covered by a standalone test: configured contexts receive a typed event, while a
context without a sink increments its observable dropped-message count and never prints directly.

Gap: exact bus/address-error frames, trace corner cases, reset-device effects, and instruction-by-
instruction differential evidence against a full PUAE oracle remain unverified.

### S003 — Image-aware overrides

Evidence: focused tests prove a matching generation enters a native override, its scoped original call
executes the upstream guest body without recursion, and an image replacement makes the old key stale.

### S004 — Bounded exits

Evidence: focused tests prove instruction-budget, architectural-exception, interrupt, halted, and
unmapped-access exits. Interrupt entry does not inflate the executed-instruction denominator.

### S005 — Linux x86-64 gameplay

Missing capability: integrate a title and measure representative interactive gameplay on Linux
x86-64; the focused core slice is not gameplay evidence.

### S006 — Apple Silicon macOS gameplay

Missing capability: build and measure representative interactive gameplay on Apple Silicon macOS;
no host evidence exists.

### S007 — Android arm64-v8a gameplay

Missing capability: build and measure representative interactive gameplay on Android arm64-v8a;
no host evidence exists.

### S008 — Linux x86-64 hosted CI

Evidence: the pinned workflow calls the canonical Python verifier on Ubuntu 24.04 with Clang/Ninja. The same
path has passed locally and in hosted run `33894071904` at revision `a706a4a`.

### S009 — Windows x86-64 hosted CI

The pinned workflow configures the complete fork-backed runtime with clang-cl/Ninja, runs synthetic
execution and link audits, and applies clang-format and clang-tidy through the canonical verifier.
The fork's embed target owns a Windows-specific feature surface instead of inheriting unavailable
POSIX headers from the libretro host configuration; a local clang-cl Windows-target syntax check
accepts every embedded CPU translation unit.

The embedded source profile now instantiates only 68000 prefetch tables and excludes full-machine
exception/MMU builders. An intentionally unresolved device fixture must fail linking; a positive
target links and runs the complete selected runtime archive without section removal. Its local final
artifact retains all 1,540 table-12 handlers and no forbidden frontend/device symbols. Native and
Windows header-profile tests run with the parent suite. COFF resolves undefined references before
removing dead COMDAT sections; section flags cannot compensate for compiling unowned functions.

Hosted run `33960527840` at revision `55e2899` passed all 23 Windows compile/link steps, then exposed
that PE's COFF symbol table is empty. The audit now reads public definitions from the sibling linker
PDB only after matching its GUID and age against the final executable's CodeView record. Missing,
mismatched, empty, and malformed evidence fails closed; no input archive is substituted. The same
1,540-handler and forbidden-owner policy applies. Local parser verification reads LLVM's existing
redistributable `llvm-symbolizer/pdb/Inputs/test.exe` and matching `test.pdb`: 1,456 public definitions,
including its three known function probes. This non-CPU fixture correctly fails the CPU-table gate;
controlled identity, record, and forbidden-owner mutations exercise refusals.

Hosted run `33961343783` at revision `47623c6` passed the final PE/PDB audit with 2,867 definitions,
all 1,540 handlers, and zero forbidden symbols, then passed all five runtime/profile tests. Its
remaining failure was the test executable's potentially throwing iostream reporting outside its
exception boundary. Terminal test reports now use nonthrowing stdio, success output errors fail the
test, and controlled standard/unknown exceptions must produce ordinary failure exits.

Evidence: [hosted run 33961881866](https://github.com/SomeoneIsWorking/amigaport/actions/runs/33961881866)
at main commit `e8f2e30757f27c39621805370fff75d74eb5b939` passed the complete Windows
synthetic runtime, linked-symbol, formatting, lint, and terminal-failure regression gate. The same
run passed Linux x86-64, Apple Silicon macOS, and Android cross-build/x86-64 emulator jobs. This is
asset-free framework evidence; representative gameplay and Android arm64-v8a execution remain the
gaps recorded in S005–S007 and S011.

### S010 — Apple Silicon macOS hosted CI

The pinned workflow selects GitHub's macOS 26 arm64 runner and AppleClang, then runs the complete
synthetic verifier and Mach-O-aware linked-symbol audit. It resolves the active macOS SDK and
AppleClang's libc++ search directories, records the SDK in the compile database, and supplies the
same C++ headers and SDK alongside the matching Homebrew resource directory to clang-tidy.
Intel macOS is not a current product target.

Evidence: hosted run `33958279504` at revision `c57e66e` passed the full Apple Silicon compiler, linked
runtime audit, execution, and linter gate. The compiler resolver preserves the `clang++` invocation
name, with a regression test covering its alias to `clang`.

### S011 — Android hosted CI

The Android job consumes `shared/android-port` at its pinned revision and NDK 28.2.13676358/API 21.
It builds and audits x86-64 and arm64-v8a artifacts on Ubuntu, explicitly grants the ephemeral
runner access to its KVM device, then runs the synthetic runtime on the hardware-accelerated API 35
x86-64 emulator selected by exact ADB serial. Hosted run `33957841121` passed both cross-builds,
linked audits, and x86-64 emulator execution. This does not
verify arm64-v8a execution;
that requires a matching physical or hosted ARM64 Android device. APK assembly and installation are
inapplicable because `amigaport` is an embeddable static library rather than an application package.

Evidence: the focused local gate cross-builds and link-audits both ABIs against the pinned PUAE fork,
and controlled tests prove the device boundary accepts only the configured online emulator. Source
policy tests also prove that owned shell automation remains rejected while a dependency checkout
under the authoritative `build/` root is not treated as first-party source.

Gap: matching-device arm64-v8a execution remains missing.
