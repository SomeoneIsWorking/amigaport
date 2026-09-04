#include "puae_core.hpp"

#include "puae_bridge.h"

#include <algorithm>
#include <mutex>

namespace amigaport::detail {
namespace {

std::mutex puae_core_mutex;

amigaport_puae_snapshot import_state(const CpuState &state) {
    amigaport_puae_snapshot snapshot{};
    std::copy(state.data.begin(), state.data.end(), snapshot.registers);
    std::copy(state.address.begin(), state.address.end(), snapshot.registers + 8);
    snapshot.pc = state.pc;
    snapshot.sr = state.sr;
    snapshot.usp = state.user_stack_pointer;
    snapshot.ssp = state.supervisor_stack_pointer;
    snapshot.ir = state.instruction_register;
    snapshot.irc = state.prefetch_word;
    snapshot.stopped = state.stopped;
    snapshot.halted = state.halted;
    return snapshot;
}

void export_state(CpuState &state, const amigaport_puae_snapshot &snapshot) {
    std::copy_n(snapshot.registers, 8, state.data.begin());
    std::copy_n(snapshot.registers + 8, 8, state.address.begin());
    state.pc = snapshot.pc;
    state.sr = snapshot.sr;
    state.user_stack_pointer = snapshot.usp;
    state.supervisor_stack_pointer = snapshot.ssp;
    state.instruction_register = snapshot.ir;
    state.prefetch_word = snapshot.irc;
    state.stopped = snapshot.stopped != 0;
    state.halted = snapshot.halted != 0;
}

} // namespace

CoreStep PuaeCore::step(CpuState &state, std::uint16_t instruction_word) {
    std::scoped_lock lock(puae_core_mutex);
    auto snapshot = import_state(state);
    const amigaport_puae_step result = amigaport_puae_execute_one(&snapshot, instruction_word);
    if (result.supported == 0U) {
        return {.supported = false, .cycles = 0};
    }

    export_state(state, snapshot);
    return {.supported = true, .cycles = result.cycles};
}

} // namespace amigaport::detail
