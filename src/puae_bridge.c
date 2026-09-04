#include "puae_bridge.h"

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"

uae_u32 op_4e71_0_ff(uae_u32 opcode);
uae_u32 op_7000_0_ff(uae_u32 opcode);

struct regstruct regs;
struct flag_struct regflags;
static uae_u8 instruction_window[8];

static void import_snapshot(const struct amigaport_puae_snapshot *snapshot) {
    for (unsigned index = 0; index < 16U; ++index) {
        regs.regs[index] = snapshot->registers[index];
    }
    regs.pc = snapshot->pc;
    regs.pc_p = instruction_window;
    regs.pc_oldp = instruction_window;
    regs.sr = snapshot->sr;
    regs.usp = snapshot->usp;
    regs.isp = snapshot->ssp;
    regs.ir = snapshot->ir;
    regs.irc = snapshot->irc;
    regs.s = (snapshot->sr & 0x2000U) != 0U;
    regs.t1 = (snapshot->sr & 0x8000U) != 0U;
    regs.t0 = (snapshot->sr & 0x4000U) != 0U;
    regs.intmask = (int)((snapshot->sr >> 8U) & 7U);
    regs.stopped = snapshot->stopped;
    regs.halted = snapshot->halted;
    SET_XFLG((snapshot->sr & 0x10U) != 0U);
    SET_NFLG((snapshot->sr & 0x08U) != 0U);
    SET_ZFLG((snapshot->sr & 0x04U) != 0U);
    SET_VFLG((snapshot->sr & 0x02U) != 0U);
    SET_CFLG((snapshot->sr & 0x01U) != 0U);
}

static void export_snapshot(struct amigaport_puae_snapshot *snapshot) {
    for (unsigned index = 0; index < 16U; ++index) {
        snapshot->registers[index] = regs.regs[index];
    }
    snapshot->pc = m68k_getpc();
    snapshot->sr =
        (uint16_t)((regs.t1 ? 0x8000U : 0U) | (regs.t0 ? 0x4000U : 0U) | (regs.s ? 0x2000U : 0U) |
                   (((unsigned)regs.intmask & 7U) << 8U) | (GET_XFLG() ? 0x10U : 0U) |
                   (GET_NFLG() ? 0x08U : 0U) | (GET_ZFLG() ? 0x04U : 0U) |
                   (GET_VFLG() ? 0x02U : 0U) | (GET_CFLG() ? 0x01U : 0U));
    snapshot->usp = regs.usp;
    snapshot->ssp = regs.isp;
    snapshot->ir = regs.ir;
    snapshot->irc = regs.irc;
    snapshot->stopped = regs.stopped != 0;
    snapshot->halted = regs.halted != 0;
}

struct amigaport_puae_step amigaport_puae_execute_one(struct amigaport_puae_snapshot *snapshot,
                                                      uint16_t instruction_word) {
    import_snapshot(snapshot);

    uae_u32 packed_cycles = 0;
    if (instruction_word == 0x4E71U) {
        packed_cycles = op_4e71_0_ff(instruction_word);
    } else if ((instruction_word & 0xF100U) == 0x7000U) {
        packed_cycles = op_7000_0_ff(instruction_word);
    } else {
        return (struct amigaport_puae_step){.supported = 0, .cycles = 0};
    }

    export_snapshot(snapshot);
    const uint32_t scaled_cycles = packed_cycles & 0xFFFFU;
    return (struct amigaport_puae_step){.supported = 1,
                                        .cycles = scaled_cycles / (CYCLE_UNIT / 2U)};
}
