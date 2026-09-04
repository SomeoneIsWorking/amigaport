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
| S004 | Bounded exits preserve precise guest identity, instruction counts, cycles, faults, and unsupported bytes | verified | S001 | G001 |
| S005 | Representative gameplay is conformant and performant on Linux x86-64 | missing | S001 | G001 |
| S006 | Representative gameplay is conformant and performant on Apple Silicon macOS | missing | S001 | G001 |
| S007 | Representative gameplay is conformant and performant on Android arm64-v8a | missing | S001 | G001 |

### S001 — Maintained CPU execution

The first executable slice fetches live guest words and executes NOP and the complete MOVEQ encoding
family through libretro-uae's maintained `cpuemu_0.c` handlers. All other instructions fail closed
with the image-qualified PC and opcode. The remaining PUAE CPU/core coupling must be extracted in the
maintained fork before complete instruction coverage can be claimed.

Gap: integrate the complete upstream 68000 table, memory, exception, interrupt, and timing paths
without linking PUAE's libretro frontend or Amiga device owners.

Evidence: the Linux x86-64 linked-artifact audit reaches the two intended PUAE handlers and reports
zero generic frontend/device-owner symbols.

### S002 — Complete state owner

The public context owns all D/A registers, PC, SR, USP/SSP, interrupt level, prefetch words, stopped/
halted state, current exception identity, and monotonic instruction/cycle counters. The first slice
round-trips the fields used by NOP/MOVEQ. PUAE exception-frame execution and every remaining internal
state field are not integrated yet.

Gap: round-trip and differentially prove exception frames, supervisor transitions, interrupts,
stopped execution, prefetch state, and all cycle-affecting state through the complete core path.

### S003 — Image-aware overrides

Evidence: focused tests prove a matching generation enters a native override, its scoped original call
executes the upstream guest body without recursion, and an image replacement makes the old key stale.

### S004 — Bounded exits

Evidence: focused tests prove instruction-budget, unsupported-instruction, and unmapped-fetch exits,
including instruction/cycle denominators and precise opcode/fault identity.

### S005 — Linux x86-64 gameplay

Missing capability: integrate a title and measure representative interactive gameplay on Linux
x86-64; the focused core slice is not gameplay evidence.

### S006 — Apple Silicon macOS gameplay

Missing capability: build and measure representative interactive gameplay on Apple Silicon macOS;
no host evidence exists.

### S007 — Android arm64-v8a gameplay

Missing capability: build and measure representative interactive gameplay on Android arm64-v8a;
no host evidence exists.
