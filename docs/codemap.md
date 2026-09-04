# amigaport codemap

## Architecture

```text
title-owned authenticated image + Memory callbacks
                       |
                    Executor
             /          |          \
     image identity  overrides   PuaeCore binding
                                   |
                 pinned maintained libretro-uae CPU handlers
```

The title owns address mapping and Amiga devices. `amigaport` owns architectural CPU state,
execution identity, dispatch, bounded exits, and native interception. The pinned dependency owns
instruction semantics; its libretro frontend is not linked into the runtime.

## Ownership table

| Subsystem | Responsibility | Location | Entry point | Deep doc |
| --- | --- | --- | --- | --- |
| Public execution contract | Compose state, memory, images, overrides, and bounded execution | `include/amigaport/`, `src/executor.cpp` | `amigaport::Executor` | `docs/project-state.md` |
| Architectural state | Registers, SR, supervisor/interrupt/exception and cycle state | `include/amigaport/cpu_state.hpp`, `src/cpu_state.cpp` | `CpuState` | `docs/project-state.md` |
| Image identity | Opaque title-owned image tag and monotonically replaced generation | `include/amigaport/types.hpp`, `src/executor.cpp` | `replace_image` | `docs/project-state.md` |
| Native interception | Image-qualified registry and scoped current-key suppression | `src/override_registry.*`, `src/executor.cpp` | `register_override`, `call_original` | `docs/project-state.md` |
| Maintained CPU binding | Serialize instance state through the non-reentrant PUAE handler ABI | `src/puae_core.*` | `PuaeCore::step` | `docs/project-state.md` |
| Guest memory | Title-injected checked big-endian access | `include/amigaport/memory.hpp` | `Memory` | — |
| Diagnostics | Injected configurable logging boundary | `include/amigaport/types.hpp` | `Logger::write` | — |
| Build and policy | Pin dependency, compile only CPU semantics, verify source and linked-artifact boundaries | `CMakeLists.txt`, `tools/` | `tools/verify.py`, `tools/link_audit.py` | `README.md` |
| Focused verification | Exercise shipping state, execution, image, override, and policy seams | `tests/` | `amigaport_tests`, `test_source_policy.py` | `docs/project-state.md` |

## Where does new work go?

| Change | Owner |
| --- | --- |
| 68000 instruction semantics or PUAE reentrancy/extraction | maintained `libretro-uae` fork |
| Architectural execution, state synchronization, exceptions, bounded exits | `src/puae_core.*`, `src/executor.cpp` |
| Title memory maps or OCS/CIA/device semantics | consuming title |
| Title address or image policy | consuming title's adapter/override registry inputs |
| Build, format, lint, source limits | `CMakeLists.txt`, `tools/` |
