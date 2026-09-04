# amigaport codemap

## Architecture

```text
title-owned authenticated image + Memory callbacks
                       |
                    Executor
             /          |          \
     image identity  overrides   PuaeCore binding
                                   |
                 maintained fork PUAE::M68kEmbed target
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
| Maintained CPU API | Own opaque contexts, complete 68000 dispatch construction, callback memory, prefetch and exception entry | `third_party/libretro-uae/embed/`, `third_party/libretro-uae/sources/src/m68k_embed.c`, `third_party/libretro-uae/sources/src/include/uae/m68k_embed.h` | `PUAE::M68kEmbed`, `uae_m68k_step` | `docs/project-state.md` |
| Maintained CPU binding | Convert public C++ state and memory contracts to the fork-supported API | `src/puae_core.*` | `PuaeCore::step` | `docs/project-state.md` |
| Guest memory | Title-injected checked big-endian access | `include/amigaport/memory.hpp` | `Memory` | — |
| Diagnostics | Route typed fork diagnostics through the injected configurable logger | `include/amigaport/types.hpp`, `src/puae_core.cpp` | `Logger::write`, `uae_m68k_diagnostics` | — |
| Build and policy | Pin dependencies, compile only CPU semantics, and verify source, linked-artifact, and host-platform boundaries | `CMakeLists.txt`, `tools/`, `.github/workflows/ci.yml` | `tools/verify.py`, `tools/verification.py`, `tools/link_audit.py` | `README.md` |
| Focused verification | Exercise shipping state, execution, image, override, and policy seams | `tests/` | `amigaport_tests`, `test_source_policy.py` | `docs/project-state.md` |

## Where does new work go?

| Change | Owner |
| --- | --- |
| 68000 instruction semantics, contexts, dispatch, prefetch, or exception mechanics | maintained `libretro-uae` fork |
| Architectural execution, state synchronization, exceptions, bounded exits | `src/puae_core.*`, `src/executor.cpp` |
| Title memory maps or OCS/CIA/device semantics | consuming title |
| Title address or image policy | consuming title's adapter/override registry inputs |
| Build, format, lint, source limits | `CMakeLists.txt`, `tools/` |
