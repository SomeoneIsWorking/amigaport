#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct amigaport_puae_snapshot {
    uint32_t registers[16];
    uint32_t pc;
    uint16_t sr;
    uint32_t usp;
    uint32_t ssp;
    uint16_t ir;
    uint16_t irc;
    uint8_t stopped;
    uint8_t halted;
};

struct amigaport_puae_step {
    uint8_t supported;
    uint32_t cycles;
};

struct amigaport_puae_step amigaport_puae_execute_one(struct amigaport_puae_snapshot *snapshot,
                                                      uint16_t instruction_word);

#ifdef __cplusplus
}
#endif
