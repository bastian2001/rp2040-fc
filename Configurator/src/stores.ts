import { get, writable } from 'svelte/store';
import { invoke } from '@tauri-apps/api';
import { leBytesToInt } from './utils';

export const ConfigCmd = {
	STATUS: 0,
	TASK_STATUS: 1,
	REBOOT: 2,
	SAVE_SETTINGS: 3,
	PLAY_SOUND: 4,
	BB_FILE_LIST: 5,
	BB_FILE_INFO: 6,
	BB_FILE_DOWNLOAD: 7,
	BB_FILE_DELETE: 8,
	BB_FORMAT: 9,
	WRITE_OSD_FONT_CHARACTER: 10,
	SET_MOTORS: 11,
	GET_MOTORS: 12,
	BB_FILE_DOWNLOAD_RAW: 13,
	SET_DEBUG_LED: 14,
	CONFIGURATOR_PING: 15,
	REBOOT_TO_BOOTLOADER: 16,
	GET_NAME: 17,
	SET_NAME: 18,
	GET_PIDS: 19,
	SET_PIDS: 20,
	GET_RATES: 21,
	SET_RATES: 22,
	GET_BB_SETTINGS: 23,
	SET_BB_SETTINGS: 24,
	GET_ROTATION: 25,
	SERIAL_PASSTHROUGH: 26,
	GET_GPS_STATUS: 27,
	GET_GPS_ACCURACY: 28,
	GET_GPS_TIME: 29,
	GET_GPS_MOTION: 30,
	REBOOT_BY_WATCHDOG: 31,
	GET_CRASH_DUMP: 32,
	CLEAR_CRASH_DUMP: 33,
	CALIBRATE_ACCELEROMETER: 34,
	IND_MESSAGE: 0xc000
};

export type Command = {
	command: number;
	length: number;
	data: number[];
	dataStr: string;
	cmdType: 'request' | 'response' | 'error' | 'info';
};
function createPort() {
	// let port: any = null;
	let devices: string[] = [];
	// let reader: null | ReadableStreamDefaultReader = null;

	// let resolveOnClose: any = null;
	// let rejectOnClose: any = null;
	const { subscribe, set } = writable({
		command: 65535,
		length: 0,
		data: [],
		dataStr: '',
		cmdType: 'request'
	} as Command);

	function strToArray(str: string) {
		const data = [];
		for (let i = 0; i < str.length; i++) {
			data[i] = str.charCodeAt(i);
		}
		return data;
	}
	let cmdEnabled = true;
	const enableCommands = (en: boolean) => {
		cmdEnabled = en;
	};
	const onDisconnectHandlers: (() => void)[] = [];
	const onConnectHandlers: (() => void)[] = [];

	const sendCommand = (command: number, data: number[] = [], dataStr: string = '') => {
		if (!cmdEnabled) return new Promise((resolve: any) => resolve());
		if (data.length === 0 && dataStr !== '') data = strToArray(dataStr);
		const len = data.length;
		let checksum = 0;
		for (let i = 0; i < len; i++) {
			checksum ^= data[i];
		}
		checksum ^= command & 0xff;
		checksum ^= (command >> 8) & 0xff;
		checksum ^= len & 0xff;
		checksum ^= (len >> 8) & 0xff;
		const cmd = [
			...strToArray('_K'),
			len & 0xff,
			(len >> 8) & 0xff,
			command & 0xff,
			(command >> 8) & 0xff,
			...data,
			checksum
		];
		return new Promise((resolve: any, reject) => {
			invoke('serial_write', { data: cmd })
				.then(resolve)
				.catch((e) => {
					reject(e);
				});
		});
	};
	const sendRaw = (data: number[], dataStr: string = '') => {
		if (data.length === 0 && dataStr !== '') data = strToArray(dataStr);
		return new Promise((resolve: any, reject) => {
			invoke('serial_write', { data })
				.then(resolve)
				.catch((e) => {
					reject(e);
				});
		});
	};
	const rxBuf: number[] = [];
	let pingStarted = 0;
	const read = () => {
		invoke('serial_read')
			.then((d: unknown) => {
				rxBuf.push(...(d as number[]));
			})
			.catch((e) => {
				if (e !== 'Custom { kind: TimedOut, error: "Operation timed out" }') console.error(e);
			})
			.finally(() => {
				while (rxBuf.length >= 7) {
					if (rxBuf[0] != '_'.charCodeAt(0) || rxBuf[1] != 'K'.charCodeAt(0)) {
						rxBuf.splice(0, rxBuf.length);
					} else break;
				}
				const len = rxBuf[2] + rxBuf[3] * 256;
				if (rxBuf.length < len + 7) return;
				let checksum = 0;
				for (let i = 2; i < len + 7; i++) checksum ^= rxBuf[i];
				if (checksum !== 0) {
					rxBuf.splice(0, rxBuf.length);
					return;
				}
				const data = rxBuf.slice(6, len + 6);
				const command = rxBuf[4] + rxBuf[5] * 256;
				rxBuf.splice(0, len + 7);
				let dataStr = '';
				for (let i = 0; i < data.length; i++) {
					dataStr += String.fromCharCode(data[i]);
				}
				let cmdType: 'request' | 'info' | 'response' | 'error' = 'info';
				if (command < 0x4000) cmdType = 'request';
				else if (command < 0x8000) cmdType = 'response';
				else if (command < 0xc000) cmdType = 'error';
				const c: Command = {
					command,
					data,
					dataStr,
					cmdType,
					length: len
				};
				set(c);
				if (c.command === (ConfigCmd.CONFIGURATOR_PING | 0x4000)) {
					pingTime.fromConfigurator = Date.now() - pingStarted;
					//pong response from FC received
				} else if (c.command === (ConfigCmd.CONFIGURATOR_PING | 0xc000)) {
					pingTime.fromFC = leBytesToInt(c.data);
				}
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
					readInterval = setInterval(read, 5);
					pingInterval = setInterval(() => {
						sendCommand(ConfigCmd.CONFIGURATOR_PING).catch(() => {});
						pingStarted = Date.now();
					}, 200);
					statusInterval = setInterval(() => {
						sendCommand(ConfigCmd.STATUS).catch(() => {});
					}, 1000);
					onConnectHandlers.forEach((h) => h());
					resolve();
				})
				.catch((e) => {
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
					onDisconnectHandlers.forEach((h) => h());
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
	let pingTime = {
		fromConfigurator: -1,
		fromFC: -1
	};
	function getPingTime() {
		return pingTime;
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
