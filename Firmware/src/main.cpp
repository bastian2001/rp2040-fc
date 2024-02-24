#include "global.h"

volatile u8 setupDone = 0b00;

void setup() {
	Serial.begin(115200);
	EEPROM.begin(4096);
	Serial.println("Setup started");
	readEEPROM();
	// save crash info to EEPROM
	if (crashInfo[0] == 255) {
		Serial.println("Crash detected");
		for (int i = 0; i < 256; i++) {
			EEPROM.write(4096 - 256 + i, (u8)crashInfo[i]);
		}
		EEPROM.commit();
	}
	for (int i = 0; i < 256; i++) {
		crashInfo[i] = 0;
	}
	initDefaultSpi();
	gyroInit();
	imuInit();
	osdInit();
	initBaro();
	initGPS();
	initADC();
	modesInit();

	// init ELRS on pins 0 and 1 using Serial1 (UART0)
	ELRS = new ExpressLRS(Serial1, 420000, PIN_TX0, PIN_RX0);

	// init LEDs
	gpio_init(PIN_LED_ACTIVITY);
	gpio_set_dir(PIN_LED_ACTIVITY, GPIO_OUT);
	gpio_init(PIN_LED_DEBUG);
	gpio_set_dir(PIN_LED_DEBUG, GPIO_OUT);

	initESCs();
	initBlackbox();
	initSpeaker();
	rp2040.wdt_begin(1000);

	Serial.println("Setup complete");
	extern elapsedMicros taskTimer0;
	taskTimer0 = 0;
	setupDone |= 0b01;
	while (!(setupDone & 0b10)) {
		rp2040.wdt_reset();
	}
	// makeRtttlSound("o=6,b=800:1c#6,1d#6,1g#6.,1d#6$1,1g#6.$1,1d#6$2,1g#6$2");
	// makeRtttlSound("NokiaTune:d=4,o=5,b=160:8e6$4,8d6$4,4f#5$4,4g#5$4,8c#6$4,8b5$4,4d5$4,4e5$4,8b5$4,8a5$4,4c#5$4,4e5$4,1a5$4");
	makeSound(1000, 100, 100, 0);
}

elapsedMillis activityTimer;

elapsedMicros taskTimer0;
void loop() {
	tasks[TASK_LOOP0].runCounter++;
	u32 duration0 = taskTimer0;
	if (duration0 > tasks[TASK_LOOP0].maxGap) {
		tasks[TASK_LOOP0].maxGap = duration0;
	}
	taskTimer0 = 0;
	speakerLoop();
	evalBaroLoop();
	blackboxLoop();
	ELRS->loop();
	modesLoop();
	adcLoop();
	serialLoop();
	configuratorLoop();
	gpsLoop();
	taskManagerLoop();
	rp2040.wdt_reset();
	if (activityTimer >= 500) {
		gpio_put(PIN_LED_ACTIVITY, !gpio_get(PIN_LED_ACTIVITY));
		activityTimer = 0;
	}
	duration0 = taskTimer0;
	tasks[TASK_LOOP0].totalDuration += duration0;
	if (duration0 > tasks[TASK_LOOP0].maxDuration) {
		tasks[TASK_LOOP0].maxDuration = duration0;
	}
	if (duration0 < tasks[TASK_LOOP0].minDuration) {
		tasks[TASK_LOOP0].minDuration = duration0;
	}
	taskTimer0 = 0;
}

u32 *speakerRxPacket;
void setup1() {
	setupDone |= 0b10;
	speakerRxPacket = new u32[SPEAKER_SIZE];
	while (!(setupDone & 0b01)) {
	}
}
elapsedMicros taskTimer = 0;
u32 taskState           = 0;
u32 speakerRxPointer    = 0;

extern PIO speakerPio;
extern u8 speakerSm;
void loop1() {
	tasks[TASK_LOOP1].runCounter++;
	u32 duration = taskTimer;
	if (duration > tasks[TASK_LOOP1].maxGap) {
		tasks[TASK_LOOP1].maxGap = duration;
	}
	taskTimer = 0;

	tasks[TASK_SPEAKER].debugInfo = speakerRxPacket[speakerRxPointer];
	if (pio_sm_is_tx_fifo_empty(speakerPio, speakerSm)) {
		tasks[TASK_SPEAKER].errorCount++;
	}
	if (speakerRxPointer < SPEAKER_SIZE)
		while (speakerRxPointer < SPEAKER_SIZE && !pio_sm_is_tx_fifo_full(speakerPio, speakerSm)) {
			pio_sm_put_blocking(speakerPio, speakerSm, speakerRxPacket[speakerRxPointer++]);
		}
	else {
		if (rp2040.fifo.available()) {
			delete[] speakerRxPacket;
			rp2040.fifo.pop_nb((u32 *)&speakerRxPacket);
			speakerRxPointer = 0;
		}
	}
	gyroLoop();
	if (gyroUpdateFlag & 1) {
		switch (taskState++) {
		case 0:
			osdLoop(); // slow, but both need to be on this core, due to SPI collision
			break;
		case 1:
			readBaroLoop();
			break;
		}
		if (taskState == 2) taskState = 0;
		gyroUpdateFlag &= ~1;
	}
	duration = taskTimer;
	tasks[TASK_LOOP1].totalDuration += duration;
	if (duration > tasks[TASK_LOOP1].maxDuration) {
		tasks[TASK_LOOP1].maxDuration = duration;
	}
	if (duration < tasks[TASK_LOOP1].minDuration) {
		tasks[TASK_LOOP1].minDuration = duration;
	}
	taskTimer = 0;
}