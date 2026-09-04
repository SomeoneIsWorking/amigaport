# amigaport working agreement

`amigaport` is a title-neutral embeddable 68000 runtime for native Amiga ports. Read
`docs/project-goals.md`, `docs/project-state.md`, and `docs/codemap.md` before non-trivial work.

- The maintained `libretro-uae` fork owns instruction semantics. Extend its embeddable CPU boundary
  rather than copying handlers or writing a second decoder. Its libretro frontend and Amiga devices
  are diagnostic/reference material, never product dependencies.
- One `Executor` instance owns architectural state, image generation, dispatch, overrides, and
  bounded exits. The upstream core's global ABI is serialized and transient until the maintained
  fork becomes reentrant; never expose those globals as the public state owner.
- Every unsupported instruction or unsafe memory access fails closed with image-qualified PC,
  reason, bytes, and instruction/cycle denominators. Never add a silent no-op or title-address fix.
- Overrides are keyed by a title-owned opaque image tag, generation, and address. `call_original` suppresses only the
  active key for one scoped call and must execute through the ordinary maintained CPU path.
- Product code receives typed configuration and a logger. Only an explicit configuration owner may
  inspect the process environment; only the logging owner may write to process streams.
- Keep modules cohesive and below 1,200 lines. Extend `tools/source_policy.py` with positive and
  controlled-negative coverage when adding a boundary rule.
- Project tooling is modular Python; do not add shell automation. Use Clang, Ninja, clang-format,
  clang-tidy, and `python tools/verify.py` for the local gate.
- Builds belong under `build/`; disposable bounded output belongs under `scratch/`. Never use raw
  `rm` or commit game files.
