# amigaport

`amigaport` is an embeddable, title-neutral 68000 execution boundary for native Amiga game ports.
It owns architectural CPU state, runtime image identity, bounded execution, and image-qualified native
overrides. Titles inject memory and device behavior; the library never owns a generic emulator UI or
game files.

The current milestone is deliberately narrow: it compiles NOP and MOVEQ semantics directly from the
pinned maintained `SomeoneIsWorking/libretro-uae` fork and proves live guest fetch, cycle/state
accounting, image replacement, native override, and scoped-original dispatch. Complete instruction
and exception coverage, Benefactor integration, and gameplay qualification remain missing; see
`docs/project-state.md`.

## Build and verify

```sh
git submodule update --init --recursive
uv run --frozen python tools/verify.py
```

The verified local host is Linux x86-64 with Clang and Ninja. Apple Silicon macOS and Android
arm64-v8a are architectural targets, not tested or claimed by this milestone.

## Dependency and license

The pinned CPU source is `SomeoneIsWorking/libretro-uae` revision
`bae4dbe42dfef782d679eccf5e62c9622ebb8dd9`, licensed under GPL-2.0. Linking its CPU handlers makes
the distributed combined work subject to GPL-2.0. The dependency's libretro frontend and Amiga
device emulator are not linked into the runtime library.
