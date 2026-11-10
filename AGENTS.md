# amigaport working agreement

`amigaport` is a title-neutral embeddable 68000 runtime for native Amiga ports. Goals, state and
module map: `docs/project-goals.md`, `docs/project-state.md`, `docs/codemap.md`.

- The maintained `libretro-uae` fork owns instruction semantics. Extend its embeddable CPU boundary
  rather than copying handlers or writing a second decoder. Its libretro frontend and Amiga devices
  are diagnostic/reference material, never product dependencies.
- One `Executor` instance owns architectural state, image generation, dispatch, overrides, and
  bounded exits. Consume the fork's `PUAE::M68kEmbed` target and opaque context API; never expose or
  reconstruct PUAE globals in this repository.
- Every unsafe memory access fails closed with image-qualified PC, reason, bytes, and instruction/
  cycle denominators. Illegal, Line-A, and Line-F opcodes enter their architectural vectors through
  the ordinary complete table.
- Overrides are keyed by a title-owned opaque image tag, generation, and address. `call_original`
  suppresses only the active key for one scoped call and must execute through the ordinary
  maintained CPU path.
- `tools/verify.py` is the single local and hosted verification entry point (`uv run --frozen python
  tools/verify.py`). Android verification consumes the pinned `shared/android-port` contract.
- Keep modules cohesive and below 1,200 lines. Extend `tools/source_policy.py` with positive and
  controlled-negative coverage when adding a boundary rule.
