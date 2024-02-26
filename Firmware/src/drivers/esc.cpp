#include "global.h"

PIO escPio;
u8 escSm;

u32 motorPacket[2] = {0, 0xF000F}; // all motors off, but request telemetry in 12th bit
u32 escPioOffset   = 0;

u32 escRawReturn[32]         = {0};
u32 escReturn[4][8]          = {0};
u32 escEdgeDetectedReturn[4] = {0};
u8 escDmaChannel             = 0;
dma_channel_config escDmaConfig;

void initESCs() {
	escPio          = pio0;
	escPioOffset    = pio_add_program(escPio, &bidir_dshot_x4_program);
	escSm           = pio_claim_unused_sm(escPio, true);
	pio_sm_config c = bidir_dshot_x4_program_get_default_config(escPioOffset);

	pio_gpio_init(escPio, PIN_MOTORS);
	pio_gpio_init(escPio, PIN_MOTORS + 1);
	pio_gpio_init(escPio, PIN_MOTORS + 2);
	pio_gpio_init(escPio, PIN_MOTORS + 3);
	gpio_set_pulls(PIN_MOTORS, true, false);
	gpio_set_pulls(PIN_MOTORS + 1, true, false);
	gpio_set_pulls(PIN_MOTORS + 2, true, false);
	gpio_set_pulls(PIN_MOTORS + 3, true, false);
	sm_config_set_set_pins(&c, PIN_MOTORS, 4);
	sm_config_set_out_pins(&c, PIN_MOTORS, 4);
	sm_config_set_in_pins(&c, PIN_MOTORS);
	sm_config_set_jmp_pin(&c, PIN_MOTORS);

	sm_config_set_out_shift(&c, false, true, 32);
	sm_config_set_in_shift(&c, false, true, 32);

	pio_sm_init(escPio, escSm, escPioOffset, &c);
	pio_sm_set_enabled(escPio, escSm, true);
	pio_sm_set_clkdiv_int_frac(escPio, escSm, bidir_dshot_x4_CLKDIV_300_INT, bidir_dshot_x4_CLKDIV_300_FRAC);

	escDmaChannel = dma_claim_unused_channel(true);
	escDmaConfig  = dma_channel_get_default_config(escDmaChannel);
	channel_config_set_transfer_data_size(&escDmaConfig, DMA_SIZE_32);
	channel_config_set_read_increment(&escDmaConfig, false);
	channel_config_set_write_increment(&escDmaConfig, true);
	channel_config_set_dreq(&escDmaConfig, pio_get_dreq(escPio, escSm, false));
	dma_channel_set_config(escDmaChannel, &escDmaConfig, false);
	dma_channel_set_read_addr(escDmaChannel, &escPio->rxf[escSm], false);
	dma_channel_set_write_addr(escDmaChannel, escRawReturn, false);
	dma_channel_set_trans_count(escDmaChannel, 32, true);
}

u16 appendChecksum(u16 data) {
	int csum = data;
	csum ^= data >> 4;
	csum ^= data >> 8;
	csum = ~csum;
	csum &= 0xF;
	return (data << 4) | csum;
}

void sendRaw16Bit(const u16 raw[4]) {
	motorPacket[0] = 0;
	motorPacket[1] = 0;
	for (int i = 31; i >= 0; i--) {
		int pos   = i / 4;
		int motor = i % 4;
		motorPacket[0] |= ((raw[motor] >> (pos + 8)) & 1) << i;
		motorPacket[1] |= ((raw[motor] >> pos) & 1) << i;
	}
	while (!pio_sm_is_rx_fifo_empty(escPio, escSm))
		pio_sm_get(escPio, escSm);
	dma_channel_set_write_addr(escDmaChannel, escRawReturn, true);
	pio_sm_put(escPio, escSm, ~(motorPacket[0]));
	pio_sm_put(escPio, escSm, ~(motorPacket[1]));
	pio_sm_exec(escPio, escSm, pio_encode_set(pio_pindirs, 0xF));
	pio_sm_exec(escPio, escSm, pio_encode_jmp(escPioOffset));
	delayMicroseconds(200);
	Serial.printf("PC: %d\n", pio_sm_get_pc(escPio, escSm) - escPioOffset);
}

void sendRaw11Bit(const u16 raw[4]) {
	static u16 t[4] = {0, 0, 0, 0};
	for (int i = 0; i < 4; i++) {
		t[i] = constrain(raw[i], 0, 2047);
		t[i] = appendChecksum(t[i] << 1 | 1);
	}
	sendRaw16Bit(t);
}

void sendThrottles(const i16 throttles[4]) {
	static u16 t[4] = {0, 0, 0, 0};
	for (int i = 0; i < 4; i++) {
		t[i] = constrain(throttles[i], 0, 2000);
		if (t[i])
			t[i] += 47;
		t[i] = appendChecksum(t[i] << 1 | 0);
	}
	sendRaw16Bit(t);
}