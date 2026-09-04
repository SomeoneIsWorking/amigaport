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
| S008 | Asset-free Linux x86-64 hosted CI builds, lints, audits, and runs the synthetic runtime | partial | S001 | G001 |
| S009 | Asset-free Windows x86-64 hosted CI builds, lints, audits, and runs the synthetic runtime | partial | S001 | G001 |
| S010 | Asset-free Apple Silicon macOS hosted CI builds, lints, audits, and runs the synthetic runtime | partial | S001 | G001 |
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

The pinned workflow calls the canonical Python verifier on Ubuntu 24.04 with Clang/Ninja. The same
path has passed locally.

Gap: the new hosted job has not run yet; its first successful result is required before this item
becomes verified.

### S009 — Windows x86-64 hosted CI

The pinned workflow configures the complete fork-backed runtime with clang-cl/Ninja, runs synthetic
execution and link audits, and applies clang-format and clang-tidy through the canonical verifier.

Gap: no hosted result exists yet, so Windows runtime support is not claimed.

### S010 — Apple Silicon macOS hosted CI

The pinned workflow selects GitHub's macOS 26 arm64 runner and AppleClang, then runs the complete
synthetic verifier and Mach-O-aware linked-symbol audit. No hosted result exists yet. Intel macOS is
not a current project target; the project goal specifically names Apple Silicon.

Gap: no hosted Apple Silicon result exists yet.

### S011 — Android hosted CI

The Android job consumes `shared/android-port` at its pinned revision and NDK 28.2.13676358/API 21.
It builds and audits x86-64 and arm64-v8a artifacts on a macOS Intel host, then runs the synthetic
runtime on the hardware-accelerated API 35 x86-64 emulator selected by exact ADB serial. The first
hosted result remains missing. This does not verify arm64-v8a execution;
that requires a matching physical or hosted ARM64 Android device. APK assembly and installation are
inapplicable because `amigaport` is an embeddable static library rather than an application package.

Evidence: the focused local gate cross-builds and link-audits both ABIs against the pinned PUAE fork,
and controlled tests prove the device boundary accepts only the configured online emulator.

Gap: the first hosted Android result and matching-device arm64-v8a execution both remain missing.
