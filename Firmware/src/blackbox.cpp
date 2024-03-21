#include "global.h"

u64 bbFlags        = 0;
u64 currentBBFlags = 0;

volatile bool bbLogging = false;
volatile bool fsReady   = false;

FSInfo64 fsInfo;
int currentLogNum = 0;
u8 bbFreqDivider  = 2;

RingBuffer<u8 *> bbFramePtrBuffer(64);

File blackboxFile;

i32 maxFileSize = 0;
u32 bbFrameNum = 0, newestPvtStartedAt = 0;
elapsedMicros frametime;
void blackboxLoop() {
	if (rp2040.fifo.available() && bbLogging && fsReady) {
		elapsedMicros taskTimer = 0;
		tasks[TASK_BLACKBOX].runCounter++;
		u8 *frame = (u8 *)rp2040.fifo.pop();
		u8 len    = frame[0];
		if (len > 0 && bbLogging)
			blackboxFile.write(frame + 1, len);
		free(frame);
		u32 duration = taskTimer;
		tasks[TASK_BLACKBOX].totalDuration += duration;
		if (duration < tasks[TASK_BLACKBOX].minDuration) {
			tasks[TASK_BLACKBOX].minDuration = duration;
		}
		if (duration > tasks[TASK_BLACKBOX].maxDuration) {
			tasks[TASK_BLACKBOX].maxDuration = duration;
		}
	}
}

void initBlackbox() {
#if BLACKBOX_STORAGE == LITTLEFS
	fsReady = LittleFS.begin();
	fsReady = fsReady && LittleFS.info64(fsInfo);
#elif BLACKBOX_STORAGE == SD_BB
	SPI1.setRX(PIN_SD_MISO);
	SPI1.setTX(PIN_SD_MOSI);
	SPI1.setSCK(PIN_SD_SCK);
	SDFSConfig cfg;
	cfg.setCSPin(PIN_SD_CS);
	cfg.setSPI(SPI1);
	SDFS.setConfig(cfg);
	fsReady = SDFS.begin();
	if (!SDFS.exists("/kolibri")) {
		SDFS.mkdir("/kolibri");
	}
	Serial.println(fsReady ? "SD card ready" : "SD card not ready");
#endif
}

bool clearBlackbox() {
	if (!fsReady || bbLogging)
		return false;
#if BLACKBOX_STORAGE == LITTLEFS
	LittleFS.format();
	return true;
#elif BLACKBOX_STORAGE == SD_BB
	for (int i = 0; i < 100; i++) {
		char path[32];
		snprintf(path, 32, "/kolibri/%01d.kbb", i);
		SDFS.remove(path);
		rp2040.wdt_reset();
	}
	if (!SDFS.rmdir("/kolibri")) return false;
	if (!SDFS.mkdir("/kolibri")) return false;
	return true;
#endif
}

void setFlags(u64 flags) {
	bbFlags = flags;
	EEPROM.put((u16)EEPROM_POS::BB_FLAGS, bbFlags);
}
void setDivider(u8 divider) {
	if (divider > 0)
		bbFreqDivider = divider;
	EEPROM.put((u16)EEPROM_POS::BB_FREQ_DIVIDER, bbFreqDivider);
}

void printLogBinRaw(u8 logNum) {
	char path[32];
	logNum %= 100;
	rp2040.wdt_reset();
#if BLACKBOX_STORAGE == LITTLEFS
	snprintf(path, 32, "/logs%01d/%01d.kbb", logNum / 10, logNum % 10);
	File logFile = LittleFS.open(path, "r");
#elif BLACKBOX_STORAGE == SD_BB
	snprintf(path, 32, "/kolibri/%01d.kbb", logNum);
	File logFile = SDFS.open(path, "r");
#endif
	if (!logFile)
		return;
	u32 fileSize = logFile.size();
	for (u32 i = 0; i < fileSize; i++) {
		Serial.write(logFile.read());
		if ((i % 1024) == 0) {
			rp2040.wdt_reset();
			Serial.flush();
		}
	}
	logFile.close();
}

void printLogBin(u8 logNum, i16 singleChunk) {
	char path[32];
#if BLACKBOX_STORAGE == LITTLEFS
	snprintf(path, 32, "/logs%01d/%01d.kbb", logNum / 10, logNum % 10);
	File logFile = LittleFS.open(path, "r");
#elif BLACKBOX_STORAGE == SD_BB
	snprintf(path, 32, "/kolibri/%01d.kbb", logNum);
	File logFile = SDFS.open(path, "r");
#endif
	if (!logFile) {
		sendCommand(((u16)ConfigCmd::BB_FILE_DOWNLOAD) | 0x8000, "File not found", strlen("File not found"));
		return;
	}
	u8 buffer[1027];
	buffer[0]    = logNum;
	u16 chunkNum = 0;
	if (singleChunk >= 0) {
		chunkNum = singleChunk;
		logFile.seek(chunkNum * 1024, SeekSet);
	}
	size_t bytesRead = 1;
	while (bytesRead > 0) {
		rp2040.wdt_reset();
		gpio_put(PIN_LED_ACTIVITY, chunkNum & 1);
		bytesRead = logFile.read(buffer + 3, 1024);
		buffer[1] = chunkNum & 0xFF;
		buffer[2] = chunkNum >> 8;
		sendCommand(((u16)ConfigCmd::BB_FILE_DOWNLOAD) | 0x4000, (char *)buffer, bytesRead + 3);
		Serial.flush();
		while (Serial.available())
			Serial.read();
		chunkNum++;
		if (singleChunk >= 0)
			break;
	}
	logFile.close();
	// finish frame includes 0xFFFF as chunk number, and then the actual max chunk number
	if (singleChunk >= 0)
		return;
	buffer[0] = logNum;
	buffer[1] = 0xFF;
	buffer[2] = 0xFF;
	buffer[3] = chunkNum & 0xFF;
	buffer[4] = chunkNum >> 8;
	sendCommand(((u16)ConfigCmd::BB_FILE_DOWNLOAD) | 0x4000, (char *)buffer, 5);
}

void startLogging() {
	if (!bbFlags || !fsReady || bbLogging || !bbFreqDivider)
		return;
	currentBBFlags = bbFlags;
#if BLACKBOX_STORAGE == LITTLEFS
	if (!(LittleFS.info64(fsInfo)))
		return;
	maxFileSize = fsInfo.totalBytes - fsInfo.usedBytes - 50000;
	if (maxFileSize < 20000) {
		return;
	}
#endif
	char path[32];
	for (int i = 0; i < 100; i++) {
		rp2040.wdt_reset();
#if BLACKBOX_STORAGE == LITTLEFS
		snprintf(path, 32, "/logs%01d/%01d.kbb", ((i + currentLogNum) % 100) / 10, (i + currentLogNum) % 10);
		if (!LittleFS.exists(path)) {
			currentLogNum += i + 1;
			break;
		}
#elif BLACKBOX_STORAGE == SD_BB
		snprintf(path, 32, "/kolibri/%01d.kbb", (i + currentLogNum) % 100);
		if (!SDFS.exists(path)) {
			currentLogNum += i + 1;
			break;
		}
#endif
		if (i == 99) {
			// clearBlackbox();
			return;
		}
	}
#if BLACKBOX_STORAGE == LITTLEFS
	blackboxFile = LittleFS.open(path, "a");
#elif BLACKBOX_STORAGE == SD_BB
	blackboxFile = SDFS.open(path, "a");
#endif
	if (!blackboxFile)
		return;
	const u8 data[] = {
		0x20, 0x27, 0xA1, 0x99, 0, 0, 1 // magic bytes, version
	};
	blackboxFile.write(data, 7);
	u32 recordTime = timestamp;
	if (recordTime == 0) {
		u32 m      = millis() / 1000;
		recordTime = (m % 60) & 0x3F;
		recordTime |= ((m / 60) % 60) << 6;
		recordTime |= ((m / 3600) % 24) << 12;
		recordTime |= 1 << 17; // days start in 1
		recordTime |= 1 << 22; // months start in 1
	}
	blackboxFile.write((u8 *)&recordTime, 4);
	blackboxFile.write((u8)0); // 3200Hz gyro
	blackboxFile.write((u8)bbFreqDivider);
	blackboxFile.write((u8)3); // 2000deg/sec and 16g
	i32 rf[5][3];
	for (int i = 0; i < 5; i++)
		for (int j = 0; j < 3; j++)
			rf[i][j] = rateFactors[i][j].getRaw();
	blackboxFile.write((u8 *)rf, 60);
	i32 pg[3][7];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 7; j++)
			pg[i][j] = pidGains[i][j].getRaw();
	blackboxFile.write((u8 *)pg, 84);
	blackboxFile.write((u8 *)&bbFlags, 8);
	blackboxFile.write((u8)MOTOR_POLES);
	while (blackboxFile.position() < 256) {
		blackboxFile.write((u8)0);
	}
	bbFrameNum = 0;
	bbLogging  = true;
	// 256 bytes header
	frametime = 0;
}

void endLogging() {
	if (!fsReady)
		return;
	rp2040.wdt_reset();
	if (bbLogging)
		blackboxFile.close();
	bbLogging = false;
}

void __not_in_flash_func(writeSingleFrame)() {
	u8 *bbBuffer     = (u8 *)malloc(128);
	size_t bufferPos = 1;
	if (!fsReady || !bbLogging) {
		return;
	}
#if BLACKBOX_STORAGE == LITTLEFS
	if (blackboxFile.size() > maxFileSize) {
		endLogging();
		return;
	}
#endif
	if (currentBBFlags & LOG_ROLL_ELRS_RAW) {
		bbBuffer[bufferPos++] = ELRS->channels[0];
		bbBuffer[bufferPos++] = ELRS->channels[0] >> 8;
	}
	if (currentBBFlags & LOG_PITCH_ELRS_RAW) {
		bbBuffer[bufferPos++] = ELRS->channels[1];
		bbBuffer[bufferPos++] = ELRS->channels[1] >> 8;
	}
	if (currentBBFlags & LOG_THROTTLE_ELRS_RAW) {
		bbBuffer[bufferPos++] = ELRS->channels[2];
		bbBuffer[bufferPos++] = ELRS->channels[2] >> 8;
	}
	if (currentBBFlags & LOG_YAW_ELRS_RAW) {
		bbBuffer[bufferPos++] = ELRS->channels[3];
		bbBuffer[bufferPos++] = ELRS->channels[3] >> 8;
	}
	if (currentBBFlags & LOG_ROLL_SETPOINT) {
		i16 setpoint          = (i16)(rollSetpoint.getRaw() >> 12);
		bbBuffer[bufferPos++] = setpoint;
		bbBuffer[bufferPos++] = setpoint >> 8;
	}
	if (currentBBFlags & LOG_PITCH_SETPOINT) {
		i16 setpoint          = (i16)(pitchSetpoint.getRaw() >> 12);
		bbBuffer[bufferPos++] = setpoint;
		bbBuffer[bufferPos++] = setpoint >> 8;
	}
	if (currentBBFlags & LOG_THROTTLE_SETPOINT) {
		i16 t                 = (i16)(throttle.getRaw() >> 12);
		bbBuffer[bufferPos++] = t;
		bbBuffer[bufferPos++] = t >> 8;
	}
	if (currentBBFlags & LOG_YAW_SETPOINT) {
		i16 setpoint          = (i16)(yawSetpoint.getRaw() >> 12);
		bbBuffer[bufferPos++] = setpoint;
		bbBuffer[bufferPos++] = setpoint >> 8;
	}
	if (currentBBFlags & LOG_ROLL_GYRO_RAW) {
		i16 i                 = (gyroData[AXIS_ROLL].getRaw() >> 12);
		bbBuffer[bufferPos++] = i;
		bbBuffer[bufferPos++] = i >> 8;
	}
	if (currentBBFlags & LOG_PITCH_GYRO_RAW) {
		i16 i                 = (gyroData[AXIS_PITCH].getRaw() >> 12);
		bbBuffer[bufferPos++] = i;
		bbBuffer[bufferPos++] = i >> 8;
	}
	if (currentBBFlags & LOG_YAW_GYRO_RAW) {
		i16 i                 = (gyroData[AXIS_YAW].getRaw() >> 12);
		bbBuffer[bufferPos++] = i;
		bbBuffer[bufferPos++] = i >> 8;
	}
	if (currentBBFlags & LOG_ROLL_PID_P) {
		bbBuffer[bufferPos++] = rollP.getInt();
		bbBuffer[bufferPos++] = rollP.getInt() >> 8;
	}
	if (currentBBFlags & LOG_ROLL_PID_I) {
		bbBuffer[bufferPos++] = rollI.getInt();
		bbBuffer[bufferPos++] = rollI.getInt() >> 8;
	}
	if (currentBBFlags & LOG_ROLL_PID_D) {
		bbBuffer[bufferPos++] = rollD.getInt();
		bbBuffer[bufferPos++] = rollD.getInt() >> 8;
	}
	if (currentBBFlags & LOG_ROLL_PID_FF) {
		bbBuffer[bufferPos++] = rollFF.getInt();
		bbBuffer[bufferPos++] = rollFF.getInt() >> 8;
	}
	if (currentBBFlags & LOG_ROLL_PID_S) {
		bbBuffer[bufferPos++] = rollS.getInt();
		bbBuffer[bufferPos++] = rollS.getInt() >> 8;
	}
	if (currentBBFlags & LOG_PITCH_PID_P) {
		bbBuffer[bufferPos++] = pitchP.getInt();
		bbBuffer[bufferPos++] = pitchP.getInt() >> 8;
	}
	if (currentBBFlags & LOG_PITCH_PID_I) {
		bbBuffer[bufferPos++] = pitchI.getInt();
		bbBuffer[bufferPos++] = pitchI.getInt() >> 8;
	}
	if (currentBBFlags & LOG_PITCH_PID_D) {
		bbBuffer[bufferPos++] = pitchD.getInt();
		bbBuffer[bufferPos++] = pitchD.getInt() >> 8;
	}
	if (currentBBFlags & LOG_PITCH_PID_FF) {
		bbBuffer[bufferPos++] = pitchFF.getInt();
		bbBuffer[bufferPos++] = pitchFF.getInt() >> 8;
	}
	if (currentBBFlags & LOG_PITCH_PID_S) {
		bbBuffer[bufferPos++] = pitchS.getInt();
		bbBuffer[bufferPos++] = pitchS.getInt() >> 8;
	}
	if (currentBBFlags & LOG_YAW_PID_P) {
		bbBuffer[bufferPos++] = yawP.getInt();
		bbBuffer[bufferPos++] = yawP.getInt() >> 8;
	}
	if (currentBBFlags & LOG_YAW_PID_I) {
		bbBuffer[bufferPos++] = yawI.getInt();
		bbBuffer[bufferPos++] = yawI.getInt() >> 8;
	}
	if (currentBBFlags & LOG_YAW_PID_D) {
		bbBuffer[bufferPos++] = yawD.getInt();
		bbBuffer[bufferPos++] = yawD.getInt() >> 8;
	}
	if (currentBBFlags & LOG_YAW_PID_FF) {
		bbBuffer[bufferPos++] = yawFF.getInt();
		bbBuffer[bufferPos++] = yawFF.getInt() >> 8;
	}
	if (currentBBFlags & LOG_YAW_PID_S) {
		bbBuffer[bufferPos++] = yawS.getInt();
		bbBuffer[bufferPos++] = yawS.getInt() >> 8;
	}
	if (currentBBFlags & LOG_MOTOR_OUTPUTS) {
		u64 throttles64       = throttles[(u8)MOTOR::RR] | (u64)throttles[(u8)MOTOR::FR] << 12 | (u64)throttles[(u8)MOTOR::RL] << 24 | (u64)throttles[(u8)MOTOR::FL] << 36;
		bbBuffer[bufferPos++] = throttles64;
		bbBuffer[bufferPos++] = throttles64 >> 8;
		bbBuffer[bufferPos++] = throttles64 >> 16;
		bbBuffer[bufferPos++] = throttles64 >> 24;
		bbBuffer[bufferPos++] = throttles64 >> 32;
		bbBuffer[bufferPos++] = throttles64 >> 40;
	}
	if (currentBBFlags & LOG_FRAMETIME) {
		u16 ft                = frametime;
		frametime             = 0;
		bbBuffer[bufferPos++] = ft;
		bbBuffer[bufferPos++] = ft >> 8;
	}
	if (currentBBFlags & LOG_FLIGHT_MODE) {
		bbBuffer[bufferPos++] = (u8)flightMode;
	}
	if (currentBBFlags & LOG_ALTITUDE) {
		const u32 height      = combinedAltitude.getRaw() >> 12; // 12.4 fixed point, approx. 6cm resolution, 4km altitude
		bbBuffer[bufferPos++] = height;
		bbBuffer[bufferPos++] = height >> 8;
	}
	if (currentBBFlags & LOG_VVEL) {
		const i32 vvel        = vVel.getRaw() >> 10; // 10.6 fixed point, approx. 16mm/s resolution, +-512m/s max
		bbBuffer[bufferPos++] = vvel;
		bbBuffer[bufferPos++] = vvel >> 8;
	}
	if (currentBBFlags & LOG_GPS) {
		if (bbFrameNum - newestPvtStartedAt < 46) {
			u32 pos               = (bbFrameNum - newestPvtStartedAt) * 2;
			bbBuffer[bufferPos++] = currentPvtMsg[pos];
			bbBuffer[bufferPos++] = currentPvtMsg[pos + 1];
		} else if (newPvtMessageFlag & 1) {
			// 6 magic bytes to identify the start of a new PVT message
			bbBuffer[bufferPos++] = 'G';
			bbBuffer[bufferPos++] = 'P';
			newPvtMessageFlag &= ~1;
		} else if (newPvtMessageFlag & 1 << 1) {
			bbBuffer[bufferPos++] = 'S';
			bbBuffer[bufferPos++] = 'P';
			newPvtMessageFlag &= ~(1 << 1);
		} else if (newPvtMessageFlag & 1 << 2) {
			bbBuffer[bufferPos++] = 'V';
			bbBuffer[bufferPos++] = 'T';
			newPvtMessageFlag &= ~(1 << 2);
			newestPvtStartedAt = bbFrameNum + 1;
		} else {
			// placeholder 0
			bbBuffer[bufferPos++] = 0;
			bbBuffer[bufferPos++] = 0;
		}
	}
	if (currentBBFlags & LOG_ATT_ROLL) {
		i16 r                 = roll * 10000;
		bbBuffer[bufferPos++] = r;
		bbBuffer[bufferPos++] = r >> 8;
	}
	if (currentBBFlags & LOG_ATT_PITCH) {
		i16 p                 = pitch * 10000;
		bbBuffer[bufferPos++] = p;
		bbBuffer[bufferPos++] = p >> 8;
	}
	if (currentBBFlags & LOG_ATT_YAW) {
		i16 y                 = yaw * 10000;
		bbBuffer[bufferPos++] = y;
		bbBuffer[bufferPos++] = y >> 8;
	}
	if (currentBBFlags & LOG_MOTOR_RPM) {
		u64 rpmPacket         = condensedRpm[(u8)MOTOR::RR] | condensedRpm[(u8)MOTOR::FR] << 12 | (u64)condensedRpm[(u8)MOTOR::RL] << 24 | (u64)condensedRpm[(u8)MOTOR::FL] << 36;
		bbBuffer[bufferPos++] = rpmPacket;
		bbBuffer[bufferPos++] = rpmPacket >> 8;
		bbBuffer[bufferPos++] = rpmPacket >> 16;
		bbBuffer[bufferPos++] = rpmPacket >> 24;
		bbBuffer[bufferPos++] = rpmPacket >> 32;
		bbBuffer[bufferPos++] = rpmPacket >> 40;
	}
	if (currentBBFlags & LOG_ACCEL_RAW) {
		memcpy(&bbBuffer[bufferPos], accelDataRaw, 6);
		bufferPos += 6;
	}
	if (currentBBFlags & LOG_ACCEL_FILTERED) {
		i16 i                 = accelDataFiltered[0];
		bbBuffer[bufferPos++] = i;
		bbBuffer[bufferPos++] = i >> 8;
		i                     = accelDataFiltered[1];
		bbBuffer[bufferPos++] = i;
		bbBuffer[bufferPos++] = i >> 8;
		i                     = accelDataFiltered[2];
		bbBuffer[bufferPos++] = i;
		bbBuffer[bufferPos++] = i >> 8;
	}
	if (currentBBFlags & LOG_VERTICAL_ACCEL) {
		i16 a                 = (i16)(vAccel.getRaw() >> 9);
		bbBuffer[bufferPos++] = a;
		bbBuffer[bufferPos++] = a >> 8;
	}
#if BLACKBOX_STORAGE == LITTLEFS
	blackboxFile.write(bbBuffer, bufferPos);
#elif BLACKBOX_STORAGE == SD_BB
	bbBuffer[0] = bufferPos - 1;
	bbFrameNum++;
	if (bbFramePtrBuffer.isEmpty()) {
		if (!rp2040.fifo.push_nb((u32)bbBuffer)) {
			bbFramePtrBuffer.push(bbBuffer);
		}
	} else if (!bbFramePtrBuffer.isFull()) {
		bbFramePtrBuffer.push(bbBuffer);
		for (u32 i = bbFramePtrBuffer.itemCount(); i; i--) {
			u8 *frame = bbFramePtrBuffer[0];
			if (rp2040.fifo.push_nb((u32)frame)) {
				bbFramePtrBuffer.pop();
			} else {
				break;
			}
		}
	} else {
		u8 *frame = bbFramePtrBuffer[0];
		if (rp2040.fifo.push_nb((u32)frame)) {
			bbFramePtrBuffer.pop();
			bbFramePtrBuffer.push(bbBuffer);
			for (u32 i = bbFramePtrBuffer.itemCount(); i; i--) {
				u8 *frame = bbFramePtrBuffer[0];
				if (rp2040.fifo.push_nb((u32)frame)) {
					bbFramePtrBuffer.pop();
				} else {
					break;
				}
			}
		} else {
			free(bbBuffer);
			bbFrameNum--;
			// Both FIFOs are full, we can't keep up with the logging, dropping oldest frame
		}
	}
#endif
}