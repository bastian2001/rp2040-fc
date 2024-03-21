#include "global.h"
#include "hardware/interp.h"
RingBuffer<u8> elrsBuffer(260);
u32 ExpressLRS::crcLut[256] = {0};
interp_config ExpressLRS::interpConfig0, ExpressLRS::interpConfig1;

ExpressLRS::ExpressLRS(SerialUART &elrsSerial, u32 baudrate, u8 pinTX, u8 pinRX)
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
	for (u32 i = 0; i < 256; i++) {
		u32 crc = i;
		for (u32 j = 0; j < 8; j++) {
			if (crc & 0x80)
				crc = (crc << 1) ^ 0xD5;
			else
				crc <<= 1;
		}
		crcLut[i] = crc & 0xFF;
	}
	interpConfig0 = interp_default_config();
	interp_config_set_blend(&interpConfig0, 1);
	interpConfig1 = interp_default_config();
}

ExpressLRS::~ExpressLRS() {
	elrsSerial.end();
}

void ExpressLRS::loop() {
	elapsedMicros taskTimer;
	if (frequencyTimer > 1000) {
		if (rcPacketRateCounter > 20 && rcMsgCount > 300)
			isLinkUp = true;
		else
			isLinkUp = false;
		if (lastMsgCount != msgCount) {
			isReceiverUp = true;
			lastMsgCount = msgCount;
		} else
			isReceiverUp = false;
		packetRateCounter   = 0;
		rcPacketRateCounter = 0;
		frequencyTimer      = 0;
	}
	if (elrsBuffer.itemCount() > 250) {
		elrsBuffer.clear();
		msgBufIndex = 0;
		crc         = 0;
		lastError   = ERROR_BUFFER_OVERFLOW;
		tasks[TASK_ELRS].errorCount++;
		tasks[TASK_ELRS].lastError = ERROR_BUFFER_OVERFLOW;
		errorFlag                  = true;
		errorCount++;
		return;
	}
	int maxScan = elrsBuffer.itemCount();
	if (!maxScan && telemetryTimer >= 15) {
		// if there is no data to read and the last sensor was transmitted more than 15ms ago, send one telemetry sensor at a time
		telemetryTimer = 0;
		telemBuffer[0] = CRSF_SYNC_BYTE;
		u32 telemLen   = 0;
		switch (currentTelemSensor++) {
		case 0: {
			// GPS (0x02)
			telemBuffer[1]  = 17;
			telemBuffer[2]  = CRSF_FRAMETYPE_GPS;
			telemBuffer[3]  = gpsMotion.lat >> 24;
			telemBuffer[4]  = gpsMotion.lat >> 16;
			telemBuffer[5]  = gpsMotion.lat >> 8;
			telemBuffer[6]  = gpsMotion.lat;
			telemBuffer[7]  = gpsMotion.lon >> 24;
			telemBuffer[8]  = gpsMotion.lon >> 16;
			telemBuffer[9]  = gpsMotion.lon >> 8;
			telemBuffer[10] = gpsMotion.lon;
			u16 data        = gpsMotion.gSpeed * 10 / 278; // mm/s to km/h / 10
			telemBuffer[11] = data >> 8;
			telemBuffer[12] = data;
			data            = gpsMotion.headMot / 1000; // 10^-5deg to 10^-2deg
			telemBuffer[13] = data >> 8;
			telemBuffer[14] = data;
			data            = (gpsMotion.alt + 500) / 1000 + 1000; // mm to m + 1000
			telemBuffer[15] = data >> 8;
			telemBuffer[16] = data;
			telemBuffer[17] = gpsStatus.satCount;
			u32 telemCrc    = 0;
			for (int i = 2; i < 18; i++) {
				telemCrc ^= telemBuffer[i];
				telemCrc = crcLut[telemCrc];
			}
			telemBuffer[18] = telemCrc;
			telemLen        = 19;
			break;
		}
		case 1: {
			// Vario (0x07)
			i16 data       = -gpsMotion.velD / 10; // mm/s to cm/s
			telemBuffer[1] = 4;
			telemBuffer[2] = CRSF_FRAMETYPE_VARIO;
			telemBuffer[3] = data >> 8;
			telemBuffer[4] = data;
			u32 telemCrc   = 0;
			for (int i = 2; i < 5; i++) {
				telemCrc ^= telemBuffer[i];
				telemCrc = crcLut[telemCrc];
			}
			telemBuffer[5] = telemCrc;
			telemLen       = 6;
			break;
		}
		case 2: {
			// Battery (0x08)
			telemBuffer[1]  = 10;
			telemBuffer[2]  = CRSF_FRAMETYPE_BATTERY;
			i32 data        = adcVoltage / 10; // cV to dV
			telemBuffer[3]  = data >> 8;
			telemBuffer[4]  = data;
			data            = adcCurrent * 10; // A to dA
			telemBuffer[5]  = data >> 8;
			telemBuffer[6]  = data;
			telemBuffer[7]  = 0;
			telemBuffer[8]  = 0;
			telemBuffer[9]  = 0;
			telemBuffer[10] = 0;
			u32 telemCrc    = 0;
			for (int i = 2; i < 11; i++) {
				telemCrc ^= telemBuffer[i];
				telemCrc = crcLut[telemCrc];
			}
			telemBuffer[11] = telemCrc;
			telemLen        = 12;
			break;
		}
		case 3: {
			// Baro Altitude (0x09)
			telemBuffer[1] = 6;
			telemBuffer[2] = CRSF_FRAMETYPE_BARO_ALT;
			i32 data       = combinedAltitude.getRaw() / 6554; // dm;
			data += 10000;                                     // dm + 10000
			telemBuffer[3] = data >> 8;
			telemBuffer[4] = data;
			data           = vVel.getRaw() / 655;
			telemBuffer[5] = data >> 8;
			telemBuffer[6] = data;
			u32 telemCrc   = 0;
			for (int i = 2; i < 7; i++) {
				telemCrc ^= telemBuffer[i];
				telemCrc = crcLut[telemCrc];
			}
			telemBuffer[7] = telemCrc;
			telemLen       = 8;
			break;
		}
		case 4: {
			// Attitude (0x1E)
			telemBuffer[1] = 8;
			telemBuffer[2] = CRSF_FRAMETYPE_ATTITUDE;
			i16 data       = pitch * 10000; // 10^-5 rad;
			telemBuffer[3] = data >> 8;
			telemBuffer[4] = data;
			data           = roll * 10000; // 10^-5 rad;
			telemBuffer[5] = data >> 8;
			telemBuffer[6] = data;
			data           = yaw * 10000; // 10^-5 rad;
			telemBuffer[7] = data >> 8;
			telemBuffer[8] = data;
			u32 telemCrc   = 0;
			for (int i = 2; i < 9; i++) {
				telemCrc ^= telemBuffer[i];
				telemCrc = crcLut[telemCrc];
			}
			telemBuffer[9] = telemCrc;
			telemLen       = 10;
			break;
		}
		case 5: {
			// Flight Mode (0x21)
			telemBuffer[2] = CRSF_FRAMETYPE_FLIGHTMODE;
			switch (flightMode) {
			case FLIGHT_MODE::ACRO:
				strcpy((char *)&telemBuffer[3], "Acro");
				telemBuffer[1] = 7;
				break;
			case FLIGHT_MODE::ANGLE:
				strcpy((char *)&telemBuffer[3], "Angle");
				telemBuffer[1] = 8;
				break;
			case FLIGHT_MODE::ALT_HOLD:
				strcpy((char *)&telemBuffer[3], "Altitude Hold");
				telemBuffer[1] = 16;
				break;
			case FLIGHT_MODE::GPS_VEL:
				strcpy((char *)&telemBuffer[3], "GPS Velocity");
				telemBuffer[1] = 15;
				break;
			case FLIGHT_MODE::GPS_POS:
				strcpy((char *)&telemBuffer[3], "GPS Position");
				telemBuffer[1] = 15;
				break;
			}
			u32 telemCrc = 0;
			for (int i = 2; i < 1 + telemBuffer[1]; i++) {
				telemCrc ^= telemBuffer[i];
				telemCrc = crcLut[telemCrc];
			}
			telemBuffer[1 + telemBuffer[1]] = telemCrc;
			telemLen                        = 2 + telemBuffer[1];
			break;
		}
		}
		if (currentTelemSensor > 5) currentTelemSensor = 0;
		elrsSerial.write(telemBuffer, telemLen);
		return;
	}
	taskTimer = 0;
	if (maxScan > 10) maxScan = 10;
	if (msgBufIndex > 54) maxScan = 64 - msgBufIndex;
	for (int i = 0; i < maxScan; i++) {
		msgBuffer[msgBufIndex] = elrsBuffer.pop();
		if (msgBufIndex >= 2) {
			crc ^= msgBuffer[msgBufIndex];
			crc = crcLut[crc];
		} else
			crc = 0;
		msgBufIndex++;
		if (msgBufIndex >= 2 + msgBuffer[1]) break; // if the message is complete, stop scanning, so we don't corrupt the crc
	}
	if (msgBufIndex > 0 && msgBuffer[0] != CRSF_SYNC_BYTE) {
		msgBufIndex = 0;
		crc         = 0;
		lastError   = ERROR_INVALID_PREFIX;
		tasks[TASK_ELRS].errorCount++;
		tasks[TASK_ELRS].lastError = ERROR_INVALID_PREFIX;
		errorFlag                  = true;
		errorCount++;
	}
	if (msgBufIndex >= 2 + msgBuffer[1]) {
		processMessage();
	}
	u32 duration = taskTimer;
	tasks[TASK_ELRS].totalDuration += duration;
	if (duration < tasks[TASK_ELRS].minDuration) {
		tasks[TASK_ELRS].minDuration = duration;
	}
	if (duration > tasks[TASK_ELRS].maxDuration) {
		tasks[TASK_ELRS].maxDuration = duration;
	}
}

void ExpressLRS::processMessage() {
	int size = msgBuffer[1] + 2;
	if (crc & 0xFF) // if the crc is not 0, then the message is invalid
	{
		crc = 0;
		msgBufIndex -= size;
		// shift all the bytes in the buffer to the left by size
		for (int i = 0; i < msgBufIndex; i++)
			msgBuffer[i] = msgBuffer[i + size];
		lastError = ERROR_CRC;
		errorFlag = true;
		errorCount++;
		tasks[TASK_ELRS].errorCount++;
		tasks[TASK_ELRS].lastError = ERROR_CRC;
		return;
	}

	msgCount++;
	sinceLastMessage = 0;
	packetRateCounter++;
	tasks[TASK_ELRS].runCounter++;

	switch (msgBuffer[2]) {
	case RC_CHANNELS_PACKED: {
		if (size != 26) // 16 channels * 11 bits + 3 bytes header + 1 byte crc
		{
			lastError = ERROR_INVALID_LENGTH;
			errorFlag = true;
			errorCount++;
			tasks[TASK_ELRS].errorCount++;
			tasks[TASK_ELRS].lastError = ERROR_INVALID_LENGTH;
			break;
		}
		// crsf_channels_t *crsfChannels = (crsf_channels_t *)(&msgBuffer[3]); // somehow conversion through bit-fields does not work, so manual conversion
		u64 decoder, decoder2;
		memcpy(&decoder, &msgBuffer[3], 8);
		u32 pChannels[16];
		pChannels[0] = decoder & 0x7FF;         // 0...10
		pChannels[1] = (decoder >> 11) & 0x7FF; // 11...21
		pChannels[2] = (decoder >> 22) & 0x7FF; // 22...32
		pChannels[3] = (decoder >> 33) & 0x7FF; // 33...43
		pChannels[4] = (decoder >> 44) & 0x7FF; // 44...54
		decoder >>= 55;                         // 55, 9 bits left
		memcpy(&decoder2, &msgBuffer[11], 6);
		decoder |= (decoder2 << 9);             // 57 bits left
		pChannels[5] = decoder & 0x7FF;         // 55...65
		pChannels[6] = (decoder >> 11) & 0x7FF; // 66...76
		pChannels[7] = (decoder >> 22) & 0x7FF; // 77...87
		pChannels[8] = (decoder >> 33) & 0x7FF; // 88...98
		pChannels[9] = (decoder >> 44) & 0x7FF; // 99...109
		decoder >>= 55;                         // 55, 2 bits left
		memcpy(&decoder2, &msgBuffer[17], 7);
		decoder |= (decoder2 << 2);              // 58 bits left
		pChannels[10] = decoder & 0x7FF;         // 110...120
		pChannels[11] = (decoder >> 11) & 0x7FF; // 121...131
		pChannels[12] = (decoder >> 22) & 0x7FF; // 132...142
		pChannels[13] = (decoder >> 33) & 0x7FF; // 143...153
		pChannels[14] = (decoder >> 44) & 0x7FF; // 154...164
		decoder >>= 55;                          // 55, 3 bits left
		pChannels[15] = decoder | (msgBuffer[24] << 3);
		// map pChannels (switches) to 1000-2000 and joysticks to 988-2011
		for (u8 i = 0; i < 16; i++) {
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

		newPacketFlag = 0xFFFFFFFF;
		if (pChannels[4] > 1500)
			consecutiveArmedCycles++;
		else
			consecutiveArmedCycles = 0;

		// update as fast as possible
		memcpy(lastChannels, channels, 16 * sizeof(u32));
		sinceLastRCMessage = 0;
		memcpy(channels, pChannels, 16 * sizeof(u32));
		rcPacketRateCounter++;
		rcMsgCount++;
		break;
	}
	case LINK_STATISTICS: {
		if (size != 14) // 10 info bytes + 3 bytes header + 1 byte crc
		{
			lastError = ERROR_INVALID_LENGTH;
			errorFlag = true;
			errorCount++;
			tasks[TASK_ELRS].errorCount++;
			tasks[TASK_ELRS].lastError = ERROR_INVALID_LENGTH;
			break;
		}
		uplinkRssi[0]       = -msgBuffer[3];
		uplinkRssi[1]       = -msgBuffer[4];
		uplinkLinkQuality   = msgBuffer[5];
		uplinkSNR           = msgBuffer[6];
		antennaSelection    = msgBuffer[7];
		packetRate          = msgBuffer[8];
		txPower             = powerStates[msgBuffer[9]];
		downlinkRssi        = -msgBuffer[10];
		downlinkLinkQuality = msgBuffer[11];
		downlinkSNR         = msgBuffer[12];
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
	case MSP_REQ:
		break;
	default:
		lastError = ERROR_UNSUPPORTED_COMMAND;
		tasks[TASK_ELRS].errorCount++;
		tasks[TASK_ELRS].lastError = ERROR_UNSUPPORTED_COMMAND;
		errorFlag                  = true;
		errorCount++;
		msgBufIndex -= size;
		crc = 0;
		// shift all the bytes in the buffer to the left by size
		for (int i = 0; i < msgBufIndex; i++)
			msgBuffer[i] = msgBuffer[i + size];
		return;
	}

	msgBufIndex = 0;
	crc         = 0;
}

void ExpressLRS::getSmoothChannels(u16 smoothChannels[4]) {
	// one new RC message every 4ms = 4000µs, ELRS 250Hz
	int sinceLast = sinceLastRCMessage;
	if (sinceLast > 4000) {
		sinceLast = 4000;
	}
	sinceLast = 255 * sinceLast / 4000;
	interp_set_config(interp0, 0, &interpConfig0);
	interp_set_config(interp0, 1, &interpConfig1);
	interp0->accum[1] = sinceLast;
	for (int i = 0; i < 4; i++) {
		interp0->base[0]  = lastChannels[i];
		interp0->base[1]  = channels[i];
		smoothChannels[i] = interp0->peek[1];
	}
}