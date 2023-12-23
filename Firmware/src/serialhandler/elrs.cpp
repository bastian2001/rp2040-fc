#include "global.h"
deque<uint8_t> elrsBuffer;

ExpressLRS::ExpressLRS(SerialUART &elrsSerial, uint32_t baudrate, uint8_t pinTX, uint8_t pinRX)
	: elrsSerial(elrsSerial),
	  pinTX(pinTX),
	  pinRX(pinRX),
	  baudrate(baudrate) {
	elrsSerial.end();
	elrsSerial.setTX(pinTX);
	elrsSerial.setRX(pinRX);
	elrsSerial.setCTS(UART_PIN_NOT_DEFINED);
	elrsSerial.setRTS(UART_PIN_NOT_DEFINED);
	elrsSerial.setFIFOSize(256);
	elrsSerial.begin(baudrate, SERIAL_8N1);
}

ExpressLRS::~ExpressLRS() {
	elrsSerial.end();
}

void ExpressLRS::loop() {
	crashInfo[3] = 1;
	if (elrsBuffer.size() > 250) {
		elrsBuffer.clear();
		msgBufIndex = 0;
		lastError	= ERROR_BUFFER_OVERFLOW;
		errorFlag	= true;
		errorCount++;
		crashInfo[3] = 2;
		return;
	}
	crashInfo[3] = 3;
	int maxScan	 = elrsBuffer.size();
	if (maxScan > 10) maxScan = 10;
	if (msgBufIndex > 55) maxScan = 65 - msgBufIndex;
	crashInfo[4] = maxScan;
	crashInfo[3] = 4;
	for (int i = 0; i < maxScan; i++) {
		crashInfo[3]			 = 5;
		msgBuffer[msgBufIndex++] = elrsBuffer.front();
		elrsBuffer.pop_front();
		crashInfo[3] = 6;
	}
	if (msgBufIndex > 0 && msgBuffer[0] != RX_PREFIX) {
		crashInfo[3] = 7;
		msgBufIndex	 = 0;
		lastError	 = ERROR_INVALID_PREFIX;
		errorFlag	 = true;
		errorCount++;
		crashInfo[3] = 8;
	}
	if (msgBufIndex >= 2 + msgBuffer[1] && msgBuffer[0] == RX_PREFIX) {
		crashInfo[3] = 9;
		processMessage();
		crashInfo[3] = 10;
	}
	if (sinceLastRCMessage > 500000)
		armed = false;
	crashInfo[5] = armed;
}

// from https://github.com/catphish/openuav/blob/master/firmware/src/elrs.c
//  Append a byte to a CRC-8
// 32 bit operations are used for speed, CRC of an RC packet take 22 instead of 25µs
void crc32_append(uint32_t data, uint32_t &crc) {
	crc ^= data;
	for (uint32_t i = 0; i < 8; i++) {
		if (crc & 0x80) {
			crc = (crc << 1) ^ 0xD5;
		} else {
			crc <<= 1;
		}
	}
}

void ExpressLRS::processMessage() {
	crashInfo[6] = 1;
	int size	 = msgBuffer[1] + 2;
	uint32_t crc = 0;
	for (int i = 2; i < size; i++)
		crc32_append(msgBuffer[i], crc);
	crashInfo[6] = 2;
	if (crc & 0xFF) // if the crc is not 0, then the message is invalid
	{
		crashInfo[6] = 3;
		msgBufIndex -= size;
		// shift all the bytes in the buffer to the left by size
		for (int i = 0; i < msgBufIndex; i++)
			msgBuffer[i] = msgBuffer[i + size];
		lastError = ERROR_CRC;
		errorFlag = true;
		errorCount++;
		crashInfo[6] = 4;
		return;
	}

	msgCount++;
	sinceLastMessage = 0;

	crashInfo[6] = 5;
	switch (msgBuffer[2]) {
	case RC_CHANNELS_PACKED: {
		crashInfo[6] = 6;
		if (size != 26) // 16 channels * 11 bits + 3 bytes header + 1 byte crc
		{
			lastError = ERROR_INVALID_LENGTH;
			errorFlag = true;
			errorCount++;
			crashInfo[6] = 7;
			break;
		}
		crashInfo[6]	   = 8;
		sinceLastRCMessage = 0;
		// crsf_channels_t *crsfChannels = (crsf_channels_t *)(&msgBuffer[3]); // somehow conversion through bit-fields does not work, so manual conversion
		uint64_t decoder, decoder2;
		memcpy(&decoder, &msgBuffer[3], 8);
		uint32_t pChannels[16];
		crashInfo[6] = 9;
		pChannels[0] = decoder & 0x7FF;			// 0...10
		pChannels[1] = (decoder >> 11) & 0x7FF; // 11...21
		pChannels[2] = (decoder >> 22) & 0x7FF; // 22...32
		pChannels[3] = (decoder >> 33) & 0x7FF; // 33...43
		pChannels[4] = (decoder >> 44) & 0x7FF; // 44...54
		decoder >>= 55;							// 55, 9 bits left
		memcpy(&decoder2, &msgBuffer[11], 6);
		decoder |= (decoder2 << 9);				// 57 bits left
		pChannels[5] = decoder & 0x7FF;			// 55...65
		pChannels[6] = (decoder >> 11) & 0x7FF; // 66...76
		pChannels[7] = (decoder >> 22) & 0x7FF; // 77...87
		pChannels[8] = (decoder >> 33) & 0x7FF; // 88...98
		pChannels[9] = (decoder >> 44) & 0x7FF; // 99...109
		decoder >>= 55;							// 55, 2 bits left
		memcpy(&decoder2, &msgBuffer[17], 7);
		decoder |= (decoder2 << 2);				 // 58 bits left
		pChannels[10] = decoder & 0x7FF;		 // 110...120
		pChannels[11] = (decoder >> 11) & 0x7FF; // 121...131
		pChannels[12] = (decoder >> 22) & 0x7FF; // 132...142
		pChannels[13] = (decoder >> 33) & 0x7FF; // 143...153
		pChannels[14] = (decoder >> 44) & 0x7FF; // 154...164
		decoder >>= 55;							 // 55, 3 bits left
		pChannels[15] = decoder | (msgBuffer[24] << 3);
		crashInfo[6]  = 10;
		// map pChannels (switches) to 1000-2000 and joysticks to 988-2011
		for (uint8_t i = 0; i < 16; i++) {
			if (i == 2)
				continue;
			pChannels[i] -= 174;
			pChannels[i] *= 1024;
			pChannels[i] /= 1636;
			pChannels[i] += 988;
			pChannels[i] = constrain(pChannels[i], 988, 2012);
		}
		// map pChannels (throttle) to 1000-2000
		pChannels[2] -= 174;
		pChannels[2] *= 1000;
		pChannels[2] /= 1636;
		pChannels[2] += 1000; // keep radio commands within 1000-2000
		pChannels[2] = constrain(pChannels[2], 1000, 2000);
		crashInfo[6] = 11;

		// check arming
		// arming switch and already armed, or arming switch and throttle down (and not armed on boot)
		if (pChannels[4] > 1500) {
			crashInfo[6] = 12;
			if (armed) {
				crashInfo[6] = 13;
			} else if (channels[4] < 1500 && channels[4] > 0 && pChannels[2] < 1020) {
				crashInfo[6] = 14;
				startLogging();
				crashInfo[6] = 15;
				armed		 = true;
			} else if (channels[4] < 1500) {
				crashInfo[6] = 16;
				Serial.println(pChannels[2]);
				makeSound(2500, 599, 70, 50);
				crashInfo[6] = 17;
			}
		} else {
			crashInfo[6] = 18;
			armed		 = false;
			if (channels[4] > 1500)
				// just disarmed, stop logging
				endLogging();
			crashInfo[6] = 19;
		}
		crashInfo[6] = 20;

		// update as fast as possible
		memcpy(lastChannels, channels, 16 * sizeof(uint32_t));
		memcpy(channels, pChannels, 16 * sizeof(uint32_t));
		crashInfo[6] = 21;
		break;
	}
	case LINK_STATISTICS: {
		crashInfo[6] = 22;
		if (size != 14) // 10 info bytes + 3 bytes header + 1 byte crc
		{
			lastError = ERROR_INVALID_LENGTH;
			errorFlag = true;
			errorCount++;
			crashInfo[6] = 23;
			break;
		}
		uplinkRssi[0]		= -msgBuffer[3];
		uplinkRssi[1]		= -msgBuffer[4];
		uplinkLinkQuality	= msgBuffer[5];
		uplinkSNR			= msgBuffer[6];
		antennaSelection	= msgBuffer[7];
		packetRate			= msgBuffer[8];
		txPower				= powerStates[msgBuffer[9]];
		downlinkRssi		= -msgBuffer[10];
		downlinkLinkQuality = msgBuffer[11];
		downlinkSNR			= msgBuffer[12];
		crashInfo[6]		= 24;
		break;
	}
	case DEVICE_PING:
		break;
	case DEVICE_INFO:
		break;
	case PARAMETER_SETTINGS_ENTRY:
		break;
	case PARAMETER_READ:
		break;
	case PARAMETER_WRITE:
		break;
	case COMMAND:
		break;
	case FRAMETYPE_WHATEVER:
		// Seems like this is not an error, but idk what it is
		break;
	default:
		lastError = ERROR_UNSUPPORTED_COMMAND;
		errorFlag = true;
		errorCount++;
		msgBufIndex -= size;
		// Serial.printf("Unknown command: %d, size: %d\n", msgBuffer[2], size);
		// shift all the bytes in the buffer to the left by size
		for (int i = 0; i < msgBufIndex; i++)
			msgBuffer[i] = msgBuffer[i + size];
		return;
	}
	crashInfo[6] = 25;

	msgBufIndex = 0;
}

void ExpressLRS::getSmoothChannels(uint16_t smoothChannels[4]) {
	// one new RC message every 4ms = 4000µs, ELRS 250Hz
	crashInfo[136] = 1;
	static uint32_t sum[4];
	int sinceLast = sinceLastRCMessage;
	if (sinceLast > 4000) {
		crashInfo[136] = 2;
		sinceLast	   = 4000;
	}
	for (int i = 0; i < 4; i++) {
		crashInfo[136] = 3;
		sum[i]		   = sinceLast * channels[i] + (4000 - sinceLast) * lastChannels[i];
		sum[i] /= 4000;
		smoothChannels[i] = sum[i];
		crashInfo[136]	  = 4;
	}
}