#include "puae_core.hpp"

#include "uae/m68k_embed.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace amigaport::detail {
namespace {

MemoryFault import_fault(uae_m68k_memory_status status) {
    switch (status) {
    case UAE_M68K_MEMORY_OK:
        return MemoryFault::None;
    case UAE_M68K_MEMORY_UNMAPPED:
        return MemoryFault::Unmapped;
    case UAE_M68K_MEMORY_READ_ONLY:
        return MemoryFault::ReadOnly;
    case UAE_M68K_MEMORY_MISALIGNED:
        return MemoryFault::Misaligned;
    }
    return MemoryFault::Unmapped;
}

uae_m68k_memory_status export_fault(MemoryFault fault) {
    switch (fault) {
    case MemoryFault::None:
        return UAE_M68K_MEMORY_OK;
    case MemoryFault::Unmapped:
        return UAE_M68K_MEMORY_UNMAPPED;
    case MemoryFault::Misaligned:
        return UAE_M68K_MEMORY_MISALIGNED;
    case MemoryFault::ReadOnly:
        return UAE_M68K_MEMORY_READ_ONLY;
    }
    return UAE_M68K_MEMORY_UNMAPPED;
}

uae_m68k_state import_state(const CpuState &state) {
    uae_m68k_state snapshot{};
    std::copy(state.data.begin(), state.data.end(), snapshot.data);
    std::copy(state.address.begin(), state.address.end(), snapshot.address);
    snapshot.pc = state.pc;
    snapshot.sr = state.sr;
    snapshot.usp = state.user_stack_pointer;
    snapshot.ssp = state.supervisor_stack_pointer;
    snapshot.ir = state.instruction_register;
    snapshot.irc = state.prefetch_word;
    snapshot.prefetch_address = state.prefetch_address;
    snapshot.prefetch_valid = state.prefetch_valid;
    snapshot.pending_interrupt_level = state.pending_interrupt_level;
    snapshot.stopped = state.stopped;
    snapshot.halted = state.halted;
    return snapshot;
}

void export_state(CpuState &state, const uae_m68k_state &snapshot) {
    std::copy_n(snapshot.data, 8, state.data.begin());
    std::copy_n(snapshot.address, 8, state.address.begin());
    state.pc = snapshot.pc;
    state.sr = snapshot.sr;
    state.user_stack_pointer = snapshot.usp;
    state.supervisor_stack_pointer = snapshot.ssp;
    state.instruction_register = snapshot.ir;
    state.prefetch_word = snapshot.irc;
    state.prefetch_address = snapshot.prefetch_address;
    state.prefetch_valid = snapshot.prefetch_valid != 0;
    state.pending_interrupt_level = snapshot.pending_interrupt_level;
    state.stopped = snapshot.stopped != 0;
    state.halted = snapshot.halted != 0;
}

} // namespace

class PuaeCore::Impl final {
  public:
    Impl(Memory &memory_value, Logger &logger_value) : memory(memory_value), logger(logger_value) {
        const uae_m68k_memory callbacks{.read = &read,
                                        .write = &write,
                                        .acknowledge_interrupt = nullptr,
                                        .reset_devices = nullptr};
        const uae_m68k_diagnostics diagnostics{.write = &write_log, .user = this};
        context = uae_m68k_context_create(&callbacks, this, &diagnostics);
        if (context == nullptr) {
            throw std::runtime_error("PUAE 68000 context creation failed");
        }
    }

    ~Impl() { uae_m68k_context_destroy(context); }

    static uae_m68k_memory_status read(void *user, const uae_m68k_read_request *request,
                                       std::uint32_t *value) {
        auto &self = *static_cast<Impl *>(user);
        if (request->width == 1U) {
            const auto result = self.memory.read8(request->address);
            *value = result.value;
            return export_fault(result.fault);
        }
        if (request->width == 2U) {
            const auto result = self.memory.read16(request->address);
            *value = result.value;
            return export_fault(result.fault);
        }
        const auto result = self.memory.read32(request->address);
        *value = result.value;
        return export_fault(result.fault);
    }

    static uae_m68k_memory_status write(void *user, const uae_m68k_write_request *request) {
        auto &self = *static_cast<Impl *>(user);
        if (request->width == 1U) {
            return export_fault(self.memory.write8(
                {.address = request->address, .value = static_cast<std::uint8_t>(request->value)}));
        }
        if (request->width == 2U) {
            return export_fault(
                self.memory.write16({.address = request->address,
                                     .value = static_cast<std::uint16_t>(request->value)}));
        }
        return export_fault(
            self.memory.write32({.address = request->address, .value = request->value}));
    }

    static void write_log(void *user, const uae_m68k_log_event *event) noexcept {
        auto &self = *static_cast<Impl *>(user);
        self.logger.write(LogLevel::Debug, "puae", std::string_view(event->message, event->length));
    }

    Memory &memory;
    Logger &logger;
    uae_m68k_context *context{};
};

PuaeCore::PuaeCore(Memory &memory, Logger &logger) : impl_(new Impl(memory, logger)) {}
PuaeCore::~PuaeCore() { delete impl_; }

CoreStep PuaeCore::step(CpuState &state) {
    auto snapshot = import_state(state);
    const uae_m68k_step_result result = uae_m68k_step(impl_->context, &snapshot);
    export_state(state, snapshot);
    CoreStep::Status status = CoreStep::Status::Completed;
    if (result.status == UAE_M68K_STEP_MEMORY_FAULT) {
        status = CoreStep::Status::MemoryFault;
    } else if (result.status == UAE_M68K_STEP_EXCEPTION) {
        status = CoreStep::Status::Exception;
    } else if (result.status == UAE_M68K_STEP_HALTED) {
        status = CoreStep::Status::Halted;
    }
    return {.status = status,
            .cycles = result.cycles,
            .memory_fault = import_fault(result.memory_status),
            .fault_address = result.fault_address,
            .instruction_word = result.opcode,
            .exception_vector = static_cast<ExceptionVector>(result.exception_vector),
            .instruction_executed = result.instruction_executed != 0};
}

} // namespace amigaport::detail
