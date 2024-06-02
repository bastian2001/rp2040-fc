// -------------------------------------------------- //
// This file is autogenerated by pioasm; do not edit! //
// -------------------------------------------------- //

#pragma once

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

// ---------------- //
// onewire_transmit //
// ---------------- //

#define onewire_transmit_wrap_target 2
#define onewire_transmit_wrap 6

static const uint16_t onewire_transmit_program_instructions[] = {
	0xe201, //  0: set    pins, 1                [2]
	0xe281, //  1: set    pindirs, 1             [2]
			//     .wrap_target
	0x80a0, //  2: pull   block
	0xef00, //  3: set    pins, 0                [15]
	0x6e01, //  4: out    pins, 1                [14]
	0x00e4, //  5: jmp    !osre, 4
	0xee01, //  6: set    pins, 1                [14]
			//     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program onewire_transmit_program = {
	.instructions = onewire_transmit_program_instructions,
	.length       = 7,
	.origin       = -1,
};

static inline pio_sm_config onewire_transmit_program_get_default_config(uint offset) {
	pio_sm_config c = pio_get_default_sm_config();
	sm_config_set_wrap(&c, offset + onewire_transmit_wrap_target, offset + onewire_transmit_wrap);
	return c;
}
#endif
