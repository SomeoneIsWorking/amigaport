# amigaport working agreement

`amigaport` is a title-neutral embeddable 68000 runtime for native Amiga ports. Read
`docs/project-goals.md`, `docs/project-state.md`, and `docs/codemap.md` before non-trivial work.

- The maintained `libretro-uae` fork owns instruction semantics. Extend its embeddable CPU boundary
  rather than copying handlers or writing a second decoder. Its libretro frontend and Amiga devices
  are diagnostic/reference material, never product dependencies.
- One `Executor` instance owns architectural state, image generation, dispatch, overrides, and
  bounded exits. Consume the fork's `PUAE::M68kEmbed` target and opaque context API; never expose or
  reconstruct PUAE globals in this repository.
- Every unsafe memory access fails closed with image-qualified PC, reason, bytes, and instruction/
  cycle denominators. Illegal, Line-A, and Line-F opcodes enter their architectural vectors through
  the ordinary complete table. Never add a silent no-op or title-address fix.
- Overrides are keyed by a title-owned opaque image tag, generation, and address. `call_original` suppresses only the
  active key for one scoped call and must execute through the ordinary maintained CPU path.
- Product code receives typed configuration and a logger. Only an explicit configuration owner may
  inspect the process environment; only the logging owner may write to process streams.
- `tools/verify.py` is the single local and hosted verification entry point. Workflow YAML selects
  pinned runners and actions only; build, lint, linked-symbol, and runtime policy stays in Python.
- Android verification consumes the pinned `shared/android-port` contract. It may not reconstruct
  NDK/API/ABI ownership in workflow YAML or infer arm64 runtime support from an x86-64 emulator.
- Keep modules cohesive and below 1,200 lines. Extend `tools/source_policy.py` with positive and
  controlled-negative coverage when adding a boundary rule.
- Project tooling is modular Python; do not add shell automation. Use Clang, Ninja, clang-format,
  clang-tidy, and `python tools/verify.py` for the local gate.
- Builds belong under `build/`; disposable bounded output belongs under `scratch/`. Never use raw
  `rm` or commit game files.
