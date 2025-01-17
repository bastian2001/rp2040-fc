import { writable } from 'svelte/store';
import { invoke } from '@tauri-apps/api';
import { configuratorLog } from './logStore';

export const MspFn = {
	API_VERSION: 1,
	FIRMWARE_VARIANT: 2,
	FIRMWARE_VERSION: 3,
	BOARD_INFO: 4,
	BUILD_INFO: 5,
	GET_NAME: 10,
	SET_NAME: 11,
	GET_FEATURE_CONFIG: 36,
	REBOOT: 68,
	GET_ADVANCED_CONFIG: 90,
	SET_ARMING_DISABLED: 99,
	MSP_STATUS: 101,
	GET_MOTOR: 104,
	RC: 105,
	MSP_ATTITUDE: 108,
	BOXIDS: 119,
	GET_MOTOR_3D_CONFIG: 124,
	GET_MOTOR_CONFIG: 131,
	UID: 160,
	ACC_CALIBRATION: 205,
	MAG_CALIBRATION: 206,
	SET_MOTOR: 214,
	ENABLE_4WAY_IF: 245,
	SET_RTC: 246,
	GET_RTC: 247,
	MSP_V2_FRAME: 255,

	// 0x400_ Configurator related commands
	STATUS: 0x4000,
	CONFIGURATOR_PING: 0x4001,
	IND_MESSAGE: 0x4002,

	// 0x401_ Entering special modes
	SERIAL_PASSTHROUGH: 0x4010,

	// 0x410_ Settings Meta commands
	SAVE_SETTINGS: 0x4100,

	// 0x411_ OSD settings
	WRITE_OSD_FONT_CHARACTER: 0x4110,

	// 0x412_ Blackbox
	GET_BB_SETTINGS: 0x4120,
	SET_BB_SETTINGS: 0x4121,
	BB_FILE_LIST: 0x4122,
	BB_FILE_INFO: 0x4123,
	BB_FILE_DOWNLOAD: 0x4124,
	BB_FILE_DELETE: 0x4125,
	BB_FORMAT: 0x4126,

	// 0x413_ GPS
	GET_GPS_STATUS: 0x4130,
	GET_GPS_ACCURACY: 0x4131,
	GET_GPS_TIME: 0x4132,
	GET_GPS_MOTION: 0x4133,

	// 0x414_ Magnetometer
	GET_MAG_DATA: 0x4140,

	// 0x415_ Gyro/Accel
	GET_ROTATION: 0x4150,

	// 0x416_ Barometer

	// 0x417_ Task Manager
	TASK_STATUS: 0x4170,

	// 0x418_ Receiver
	GET_RX_STATUS: 0x4180,

	// 0x41F_ Misc (not worth a category)
	GET_TZ_OFFSET: 0x41f0,
	SET_TZ_OFFSET: 0x41f1,

	// 0x42__ Tuning
	GET_PIDS: 0x4200,
	SET_PIDS: 0x4201,
	GET_RATES: 0x4202,
	SET_RATES: 0x4203,

	// 0x4F00-0x4F1F general debug tools
	GET_CRASH_DUMP: 0x4f00,
	CLEAR_CRASH_DUMP: 0x4f01,
	SET_DEBUG_LED: 0x4f02,
	PLAY_SOUND: 0x4f03

	// 0x4F20-0x4FFF temporary debug tools
};

const MspState = {
	IDLE: 0, // waiting for $
	PACKET_START: 1, // receiving M or X
	TYPE_V1: 2, // got M, receiving type byte (<, >, !)
	LEN_V1: 3, // if 255 is received in this step, inject jumbo len bytes
	JUMBO_LEN_LO_V1: 4,
	JUMBO_LEN_HI_V1: 5,
	CMD_V1: 6,
	PAYLOAD_V1: 7,
	FLAG_V2_OVER_V1: 8,
	CMD_LO_V2_OVER_V1: 9,
	CMD_HI_V2_OVER_V1: 10,
	LEN_LO_V2_OVER_V1: 11,
	LEN_HI_V2_OVER_V1: 12,
	PAYLOAD_V2_OVER_V1: 13,
	CHECKSUM_V2_OVER_V1: 14,
	CHECKSUM_V1: 15,
	TYPE_V2: 16, // got X, receiving type byte (<, >, !)
	FLAG_V2: 17,
	CMD_LO_V2: 18,
	CMD_HI_V2: 19,
	LEN_LO_V2: 20,
	LEN_HI_V2: 21,
	PAYLOAD_V2: 22,
	CHECKSUM_V2: 23
};

export const MspVersion = {
	V1_JUMBO: 0,
	V1: 1,
	V2: 2,
	V2_OVER_V1: 3,
	V2_OVER_V1_JUMBO: 4
};

export type Command = {
	command: number;
	length: number;
	data: number[];
	dataStr: string;
	cmdType: 'request' | 'response' | 'error';
	flag: number;
	version: number;
};
function createPort() {
	let devices: string[] = [];

	const { subscribe, set } = writable({
		command: NaN,
		length: 0,
		data: [],
		dataStr: '',
		cmdType: 'request',
		flag: 0,
		version: MspVersion.V2
	} as Command);

	subscribe(c => {
		if (c.cmdType === 'error') {
			console.error(structuredClone(c));
			configuratorLog.push('Error, see console for details.');
		}
		if (c.cmdType === 'response') {
			switch (c.command) {
				case MspFn.CONFIGURATOR_PING:
					// pong response from FC received
					fcPing = Date.now() - pingStarted;
					break;
			}
		}
		if (!Object.values(MspFn).includes(c.command) && !Number.isNaN(c.command)) {
			console.log(structuredClone(c));
			configuratorLog.push('Unsupported command, see console for details.');
		}
	});

	function strToArray(str: string) {
		const data = [];
		for (let i = 0; i < str.length; i++) {
			data[i] = str.charCodeAt(i);
		}
		return data;
	}
	function charToInt(c: string) {
		return c.charCodeAt(0);
	}
	let cmdEnabled = true;
	const enableCommands = (en: boolean) => {
		cmdEnabled = en;
	};
	const onDisconnectHandlers: (() => void)[] = [];
	const onConnectHandlers: (() => void)[] = [];

	const sendCommand = (
		type: 'request' | 'response' | 'error',
		command: number,
		version: number = MspVersion.V2,
		data: number[] = [],
		dataStr: string = ''
	) => {
		if (!cmdEnabled) return new Promise((resolve: any) => resolve());
		if (data.length === 0 && dataStr !== '') data = strToArray(dataStr);
		const len = data.length;

		if (len > 254 && version === MspVersion.V1) version = MspVersion.V1_JUMBO;
		if (len < 255 && version === MspVersion.V1_JUMBO) version = MspVersion.V1;
		if (len > 248 && version === MspVersion.V2_OVER_V1) version = MspVersion.V2_OVER_V1_JUMBO;
		if (len < 249 && version === MspVersion.V2_OVER_V1_JUMBO) version = MspVersion.V2_OVER_V1;

		const typeLut = {
			request: 60,
			response: 62,
			error: 33
		};
		const cmd = [charToInt('$'), charToInt(version === MspVersion.V2 ? 'X' : 'M'), typeLut[type]];
		switch (version) {
			case MspVersion.V1_JUMBO:
				cmd.push(0xff, len & 0xff, len >> 8);
				cmd.push(command & 0xff);
				break;
			case MspVersion.V1:
				cmd.push(len & 0xff);
				cmd.push(command & 0xff);
				break;
			case MspVersion.V2:
				cmd.push(0);
				cmd.push(command & 0xff, command >> 8);
				cmd.push(len & 0xff, len >> 8);
				break;
			case MspVersion.V2_OVER_V1:
				cmd.push((len + 6) & 0xff); // V1 len
				cmd.push(0xff); //  V2 trigger
				cmd.push(0); // V2 flag
				cmd.push(command & 0xff, command >> 8); // V2 command
				cmd.push(len & 0xff, len >> 8); // V2 len
				break;
			case MspVersion.V2_OVER_V1_JUMBO:
				cmd.push(0xff, (len + 6) & 0xff, (len + 6) >> 8); // V1 len, v1 jumbo len
				cmd.push(0xff); //  V2 trigger
				cmd.push(0); // V2 flag
				cmd.push(command & 0xff, command >> 8); // V2 command
				cmd.push(len & 0xff, len >> 8); // V2 len
				break;
		}
		cmd.push(...data);
		const crcV2StartLut = {
			[MspVersion.V2]: 3,
			[MspVersion.V2_OVER_V1]: 5,
			[MspVersion.V2_OVER_V1_JUMBO]: 7,
			[MspVersion.V1]: 0x7fffffff, // such that slice returns empty array
			[MspVersion.V1_JUMBO]: 0x7fffffff // such that slice returns empty array
		};

		let checksumV1 = cmd.slice(3).reduce((a, b) => a ^ b, 0),
			checksumV2 = cmd.slice(crcV2StartLut[version]).reduce(crc8DvbS2, 0);
		if ([MspVersion.V2, MspVersion.V2_OVER_V1, MspVersion.V2_OVER_V1_JUMBO].includes(version)) {
			cmd.push(checksumV2);
			checksumV1 ^= checksumV2;
		}
		if (
			[
				MspVersion.V1,
				MspVersion.V1_JUMBO,
				MspVersion.V2_OVER_V1,
				MspVersion.V2_OVER_V1_JUMBO
			].includes(version)
		) {
			cmd.push(checksumV1);
		}
		return new Promise((resolve: any, reject) => {
			invoke('serial_write', { data: cmd })
				.then(resolve)
				.catch(e => {
					reject(e);
				});
		});
	};
	const sendRaw = (data: number[], dataStr: string = '') => {
		if (data.length === 0 && dataStr !== '') data = strToArray(dataStr);
		return new Promise((resolve: any, reject) => {
			invoke('serial_write', { data })
				.then(resolve)
				.catch(e => {
					reject(e);
				});
		});
	};
	let rxBuf: number[] = [];
	let pingStarted = 0;
	let newCommand: Command = {
		command: 65535,
		length: 0,
		data: [],
		dataStr: '',
		cmdType: 'request',
		flag: 0,
		version: MspVersion.V2
	};
	let mspState = 0;
	let checksumV1 = 0,
		checksumV2 = 0;
	function crc8DvbS2(crc: number, data: number): number {
		crc ^= data;
		for (let i = 0; i < 8; i++) {
			if (crc & 0x80) {
				crc = (crc << 1) ^ 0xd5;
			} else {
				crc <<= 1;
			}
		}
		return crc & 0xff;
	}
	const read = () => {
		invoke('serial_read')
			.then(d => {
				rxBuf = d as number[];
			})
			.catch(e => {
				if (e !== 'Custom { kind: TimedOut, error: "Operation timed out" }') console.error(e);
			})
			.finally(() => {
				rxBuf.forEach(c => {
					switch (mspState) {
						case MspState.IDLE:
							if (c === 36) mspState = MspState.PACKET_START; /* $ */
							break;
						case MspState.PACKET_START:
							newCommand.data = [];
							newCommand.dataStr = '';
							if (c === 77) mspState = MspState.TYPE_V1; /* M */
							else if (c === 88) mspState = MspState.TYPE_V2; /* X */
							else mspState = MspState.IDLE;
							break;
						case MspState.TYPE_V1:
							mspState = MspState.LEN_V1;
							checksumV1 = 0;
							newCommand.version = MspVersion.V1;
							switch (c) {
								case 60: // '<'
									newCommand.cmdType = 'request';
									break;
								case 62: // '>'
									newCommand.cmdType = 'response';
									break;
								case 33: // '!'
									newCommand.cmdType = 'error';
									break;
								default:
									mspState = MspState.IDLE;
									break;
							}
							break;
						case MspState.LEN_V1:
							checksumV1 ^= c;
							if (c === 255) {
								mspState = MspState.JUMBO_LEN_LO_V1;
							} else {
								newCommand.length = c;
								mspState = MspState.CMD_V1;
							}
							break;
						case MspState.JUMBO_LEN_LO_V1:
							checksumV1 ^= c;
							newCommand.length = c;
							mspState = MspState.JUMBO_LEN_HI_V1;
							break;
						case MspState.JUMBO_LEN_HI_V1:
							checksumV1 ^= c;
							newCommand.length += c << 8;
							newCommand.version = MspVersion.V1_JUMBO;
							mspState = MspState.CMD_V1;
							break;
						case MspState.CMD_V1:
							checksumV1 ^= c;
							if (c === 255) {
								checksumV2 = 0;
								mspState = MspState.FLAG_V2_OVER_V1;
							} else {
								newCommand.command = c;
								mspState = newCommand.length > 0 ? MspState.PAYLOAD_V1 : MspState.CHECKSUM_V1;
							}
							break;
						case MspState.PAYLOAD_V1:
							newCommand.data.push(c);
							newCommand.dataStr += String.fromCharCode(c);
							checksumV1 ^= c;
							if (newCommand.data.length === newCommand.length) mspState = MspState.CHECKSUM_V1;
							break;
						case MspState.FLAG_V2_OVER_V1:
							newCommand.flag = c;
							checksumV1 ^= c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = MspState.CMD_LO_V2_OVER_V1;
							break;
						case MspState.CMD_LO_V2_OVER_V1:
							newCommand.command = c;
							checksumV1 ^= c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = MspState.CMD_HI_V2_OVER_V1;
							break;
						case MspState.CMD_HI_V2_OVER_V1:
							newCommand.command += c << 8;
							checksumV1 ^= c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							newCommand.version =
								newCommand.version === MspVersion.V1_JUMBO
									? MspVersion.V2_OVER_V1_JUMBO
									: MspVersion.V2_OVER_V1;
							mspState = MspState.LEN_LO_V2_OVER_V1;
							break;
						case MspState.LEN_LO_V2_OVER_V1:
							newCommand.length = c;
							checksumV1 ^= c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = MspState.LEN_HI_V2_OVER_V1;
							break;
						case MspState.LEN_HI_V2_OVER_V1:
							newCommand.length += c << 8;
							checksumV1 ^= c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState =
								newCommand.length > 0 ? MspState.PAYLOAD_V2_OVER_V1 : MspState.CHECKSUM_V2_OVER_V1;
							break;
						case MspState.PAYLOAD_V2_OVER_V1:
							newCommand.data.push(c);
							newCommand.dataStr += String.fromCharCode(c);
							checksumV1 ^= c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							if (newCommand.data.length === newCommand.length)
								mspState = MspState.CHECKSUM_V2_OVER_V1;
							break;
						case MspState.CHECKSUM_V2_OVER_V1:
							if (checksumV2 !== c) {
								mspState = MspState.IDLE;
								break;
							}
							checksumV1 ^= c;
							mspState = MspState.CHECKSUM_V1;
							break;
						case MspState.CHECKSUM_V1:
							if (checksumV1 === c) set(newCommand);
							mspState = MspState.IDLE;
							break;
						case MspState.TYPE_V2:
							mspState = MspState.FLAG_V2;
							newCommand.version = MspVersion.V2;
							checksumV2 = 0;
							switch (c) {
								case 60: // '<'
									newCommand.cmdType = 'request';
									break;
								case 62: // '>'
									newCommand.cmdType = 'response';
									break;
								case 33: // '!'
									newCommand.cmdType = 'error';
									break;
								default:
									mspState = MspState.IDLE;
									break;
							}
							break;
						case MspState.FLAG_V2:
							newCommand.flag = c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = MspState.CMD_LO_V2;
							break;
						case MspState.CMD_LO_V2:
							newCommand.command = c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = MspState.CMD_HI_V2;
							break;
						case MspState.CMD_HI_V2:
							newCommand.command += c << 8;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = MspState.LEN_LO_V2;
							break;
						case MspState.LEN_LO_V2:
							newCommand.length = c;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = MspState.LEN_HI_V2;
							break;
						case MspState.LEN_HI_V2:
							newCommand.length += c << 8;
							checksumV2 = crc8DvbS2(checksumV2, c);
							mspState = newCommand.length > 0 ? MspState.PAYLOAD_V2 : MspState.CHECKSUM_V2;
							break;
						case MspState.PAYLOAD_V2:
							newCommand.data.push(c);
							newCommand.dataStr += String.fromCharCode(c);
							checksumV2 = crc8DvbS2(checksumV2, c);
							if (newCommand.data.length === newCommand.length) mspState = MspState.CHECKSUM_V2;
							break;
						case MspState.CHECKSUM_V2:
							if (checksumV2 === c) set(newCommand);
							mspState = MspState.IDLE;
							break;
					}
				});
			});
	};
	let pingInterval: number = -1;
	let statusInterval: number = -1;
	let readInterval: number = -1;

	const connect = (portToOpen: any) => {
		return new Promise((resolve: any, reject) => {
			invoke('serial_open', { path: portToOpen })
				.then(() => {
					cmdEnabled = true;
					readInterval = setInterval(read, 3);
					pingInterval = setInterval(() => {
						sendCommand('request', MspFn.CONFIGURATOR_PING).catch(() => {});
						pingStarted = Date.now();
					}, 200);
					statusInterval = setInterval(() => {
						sendCommand('request', MspFn.STATUS).catch(() => {});
					}, 1000);
					onConnectHandlers.forEach(h => h());
					resolve();
				})
				.catch(e => {
					console.error(e);
					disconnect();
					reject(e);
				});
		});
	};
	const disconnect = () => {
		return new Promise((resolve: any, reject) => {
			invoke('serial_close')
				.then(() => {
					onDisconnectHandlers.forEach(h => h());
					resolve();
				})
				.catch(console.error)
				.finally(() => {
					cmdEnabled = false;
					clearInterval(readInterval);
					clearInterval(pingInterval);
					clearInterval(statusInterval);
					readInterval = -1;
					reject();
				});
		});
	};
	const getDevices = () => {
		return devices;
	};
	function listDevices() {
		invoke('serial_list').then((d: unknown) => {
			devices = d as any;
		});
	}
	setInterval(listDevices, 1000);
	listDevices();

	const addOnDisconnectHandler = (handler: () => void) => {
		onDisconnectHandlers.push(handler);
	};
	const removeOnDisconnectHandler = (handler: () => void) => {
		const i = onDisconnectHandlers.indexOf(handler);
		if (i >= 0) onDisconnectHandlers.splice(i, 1);
	};
	const addOnConnectHandler = (handler: () => void) => {
		onConnectHandlers.push(handler);
	};
	const removeOnConnectHandler = (handler: () => void) => {
		const i = onConnectHandlers.indexOf(handler);
		if (i >= 0) onConnectHandlers.splice(i, 1);
	};
	let fcPing = -1;
	function getPingTime() {
		return fcPing;
	}
	return {
		subscribe,
		set,
		sendCommand,
		connect,
		disconnect,
		getDevices,
		enableCommands,
		sendRaw,
		addOnDisconnectHandler,
		removeOnDisconnectHandler,
		addOnConnectHandler,
		removeOnConnectHandler,
		getPingTime
	};
}
export const port = createPort();
