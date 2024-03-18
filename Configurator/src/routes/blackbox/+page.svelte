<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { port, type Command, ConfigCmd } from '../../stores';
	import TracePlacer from './tracePlacer.svelte';
	import Timeline from './timeline.svelte';
	import Settings from './settings.svelte';
	import {
		leBytesToInt,
		type BBLog,
		type LogFrame,
		getNestedProperty,
		roundToDecimal
	} from '../../utils';

	type TraceInGraph = {
		flagName: string;
		color: string;
		strokeWidth: number;
		minValue: number;
		maxValue: number;
		modifier: any;
		id: number;
		unit?: string;
		states?: string[];
		decimals?: number;
		displayName?: string;
		overrideAuto?: { min: number; max: number };
	};

	let graphs: TraceInGraph[][] = [[]];

	const ACC_RANGES = [2, 4, 8, 16];
	const GYRO_RANGES = [2000, 1000, 500, 250, 125];

	let dataViewer: HTMLDivElement;

	let dataSlice: LogFrame[] = [];

	let drawFullCanvasTimeout = -1;

	$: dataSlice =
		loadedLog?.frames.slice(
			Math.max(0, Math.min(startFrame, endFrame)),
			Math.max(0, Math.max(startFrame, endFrame)) + 1
		) || [];
	$: dataSlice, drawCanvas();

	let logNums: { text: string; num: number }[] = [];

	let loadedLog: BBLog | undefined;

	let binFile: number[] = [];
	let binFileNumber = -1;
	let receivedChunks: boolean[] = [];
	let totalChunks = -1;
	let resolveWhenReady = (log: BBLog) => {};
	let rejectWrongFile = (_: string) => {};
	let startFrame = 0;
	let endFrame = 0;
	let mounted = false;

	let showSettings = false;

	const getGyroBBRange = (file: BBLog | undefined) => {
		if (!file) return { max: -2000, min: 2000 };
		let maxSetpoints = [0, 0, 0];
		for (let exp = 0; exp < 5; exp++)
			for (let ax = 0; ax < 3; ax++) maxSetpoints[ax] += file.rateFactors[exp][ax];
		//Math.max leads to a stack overflow, thus using a for loop now
		let max = 0,
			min = 0;
		for (let i = 0; i < maxSetpoints.length; i++) {
			if (maxSetpoints[i] > max) max = maxSetpoints[i];
			if (maxSetpoints[i] < min) min = maxSetpoints[i];
		}
		file.frames.forEach((f) => {
			if (f.gyro.roll! > max) max = f.gyro.roll!;
			if (f.gyro.pitch! > max) max = f.gyro.pitch!;
			if (f.gyro.yaw! > max) max = f.gyro.yaw!;
			if (f.gyro.roll! < min) min = f.gyro.roll!;
			if (f.gyro.pitch! < min) min = f.gyro.pitch!;
			if (f.gyro.yaw! < min) min = f.gyro.yaw!;
		});
		const fullRange = Math.round(Math.max(max, -min));
		return { max: fullRange, min: -fullRange };
	};
	const getAltitudeRange = (file: BBLog | undefined) => {
		if (!file) return { max: 300, min: 0 };
		let max = -20000,
			min = 20000;
		file.frames.forEach((f) => {
			if (f.motion.altitude! > max) max = f.motion.altitude!;
			if (f.motion.altitude! < min) min = f.motion.altitude!;
		});
		const range = max - min;
		max = 10 * Math.ceil(max / 10);
		min = 10 * Math.floor(min / 10);
		return { max, min };
	};

	const BB_ALL_FLAGS = {
		LOG_ROLL_ELRS_RAW: {
			name: 'Roll ELRS Raw',
			path: 'elrs.roll',
			minValue: 988,
			maxValue: 2012,
			unit: 'µs'
		},
		LOG_PITCH_ELRS_RAW: {
			name: 'Pitch ELRS Raw',
			path: 'elrs.pitch',
			minValue: 988,
			maxValue: 2012,
			unit: 'µs'
		},
		LOG_THROTTLE_ELRS_RAW: {
			name: 'Throttle ELRS Raw',
			path: 'elrs.throttle',
			minValue: 1000,
			maxValue: 2000,
			unit: 'µs'
		},
		LOG_YAW_ELRS_RAW: {
			name: 'Yaw ELRS Raw',
			path: 'elrs.yaw',
			minValue: 988,
			maxValue: 2012,
			unit: 'µs'
		},
		LOG_ROLL_SETPOINT: {
			name: 'Roll Setpoint',
			path: 'setpoint.roll',
			rangeFn: getGyroBBRange,
			unit: '°/sec'
		},
		LOG_PITCH_SETPOINT: {
			name: 'Pitch Setpoint',
			path: 'setpoint.pitch',
			rangeFn: getGyroBBRange,
			unit: '°/sec'
		},
		LOG_THROTTLE_SETPOINT: {
			name: 'Throttle Setpoint',
			path: 'setpoint.throttle',
			minValue: 1000,
			maxValue: 2000,
			unit: 'µs'
		},
		LOG_YAW_SETPOINT: {
			name: 'Yaw Setpoint',
			path: 'setpoint.yaw',
			rangeFn: getGyroBBRange,
			unit: '°/sec'
		},
		LOG_ROLL_GYRO_RAW: {
			name: 'Roll Gyro Raw',
			path: 'gyro.roll',
			rangeFn: getGyroBBRange,
			unit: '°/sec'
		},
		LOG_PITCH_GYRO_RAW: {
			name: 'Pitch Gyro Raw',
			path: 'gyro.pitch',
			rangeFn: getGyroBBRange,
			unit: '°/sec'
		},
		LOG_YAW_GYRO_RAW: {
			name: 'Yaw Gyro Raw',
			path: 'gyro.yaw',
			rangeFn: getGyroBBRange,
			unit: '°/sec'
		},
		LOG_ROLL_PID_P: {
			name: 'Roll PID P',
			path: 'pid.roll.p',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_ROLL_PID_I: {
			name: 'Roll PID I',
			path: 'pid.roll.i',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_ROLL_PID_D: {
			name: 'Roll PID D',
			path: 'pid.roll.d',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_ROLL_PID_FF: {
			name: 'Roll PID FF',
			path: 'pid.roll.ff',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_ROLL_PID_S: {
			name: 'Roll PID S',
			path: 'pid.roll.s',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_PITCH_PID_P: {
			name: 'Pitch PID P',
			path: 'pid.pitch.p',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_PITCH_PID_I: {
			name: 'Pitch PID I',
			path: 'pid.pitch.i',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_PITCH_PID_D: {
			name: 'Pitch PID D',
			path: 'pid.pitch.d',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_PITCH_PID_FF: {
			name: 'Pitch PID FF',
			path: 'pid.pitch.ff',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_PITCH_PID_S: {
			name: 'Pitch PID S',
			path: 'pid.pitch.s',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_YAW_PID_P: {
			name: 'Yaw PID P',
			path: 'pid.yaw.p',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_YAW_PID_I: {
			name: 'Yaw PID I',
			path: 'pid.yaw.i',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_YAW_PID_D: {
			name: 'Yaw PID D',
			path: 'pid.yaw.d',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_YAW_PID_FF: {
			name: 'Yaw PID FF',
			path: 'pid.yaw.ff',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_YAW_PID_S: {
			name: 'Yaw PID S',
			path: 'pid.yaw.s',
			minValue: -1500,
			maxValue: 1500,
			unit: ''
		},
		LOG_MOTOR_OUTPUTS: {
			name: 'Motor Outputs',
			path: 'motors.out',
			minValue: 0,
			maxValue: 2000,
			unit: '',
			usesModifier: true
		},
		LOG_FRAMETIME: {
			name: 'Frametime',
			path: 'frametime',
			minValue: 0,
			maxValue: 1000,
			unit: 'µs'
		},
		LOG_FLIGHT_MODE: {
			name: 'Flight Mode',
			path: 'flightMode',
			minValue: 0,
			maxValue: 4,
			states: ['Acro', 'Angle', 'Altitude Hold', 'GPS Velocity', 'GPS Position'],
			unit: ''
		},
		LOG_ALTITUDE: {
			name: 'Altitude',
			path: 'motion.altitude',
			rangeFn: getAltitudeRange,
			decimals: 2,
			unit: 'm'
		},
		LOG_VVEL: {
			name: 'Vertical Velocity',
			path: 'motion.vvel',
			minValue: -10,
			maxValue: 10,
			decimals: 2,
			unit: 'm/s'
		},
		LOG_GPS: {
			name: 'GPS',
			path: 'motion.gps',
			minValue: 0,
			maxValue: 100,
			unit: '',
			usesModifier: true
		},
		LOG_ATT_ROLL: {
			name: 'Roll Angle',
			path: 'attitude.roll',
			minValue: -180,
			maxValue: 180,
			unit: '°'
		},
		LOG_ATT_PITCH: {
			name: 'Pitch Angle',
			path: 'attitude.pitch',
			minValue: -180,
			maxValue: 180,
			unit: '°'
		},
		LOG_ATT_YAW: {
			name: 'Yaw Angle',
			path: 'attitude.yaw',
			minValue: -180,
			maxValue: 180,
			unit: '°'
		},
		LOG_MOTOR_RPM: {
			name: 'Motor RPM',
			path: 'motors.rpm',
			minValue: 0,
			maxValue: 50000,
			unit: 'rpm',
			usesModifier: true
		},
		LOG_ACCEL_RAW: {
			name: 'Accel Raw',
			path: 'motion.accelRaw',
			minValue: -40,
			maxValue: 40,
			unit: 'm/s²',
			decimals: 3,
			usesModifier: true
		},
		LOG_VERTICAL_ACCEL: {
			name: 'Vertical Accel',
			path: 'motion.accelVertical',
			minValue: -40,
			maxValue: 40,
			unit: 'm/s²',
			decimals: 3
		},
		LOG_REL_ALT: {
			name: 'vVel -> alt',
			path: 'motion.relAlt',
			minValue: -5,
			maxValue: 5,
			unit: 'm',
			decimals: 2
		},
		LOG_REL_VVEL: {
			name: 'Rel. vVel',
			path: 'motion.relVVel',
			minValue: -10,
			maxValue: 10,
			unit: 'm/s',
			decimals: 2
		}
	} as {
		[key: string]: {
			name: string;
			path: string;
			minValue?: number;
			maxValue?: number;
			rangeFn?: (file: BBLog | undefined) => { max: number; min: number };
			unit: string;
			usesModifier?: boolean;
			decimals?: number;
			states?: string[];
		};
	};

	const BB_GEN_FLAGS = {
		GEN_ROLL_SETPOINT: {
			name: 'Roll Setpoint',
			replaces: 'LOG_ROLL_SETPOINT',
			requires: ['LOG_ROLL_ELRS_RAW'],
			unit: '°/sec',
			exact: false
		},
		GEN_PITCH_SETPOINT: {
			name: 'Pitch Setpoint',
			replaces: 'LOG_PITCH_SETPOINT',
			requires: ['LOG_PITCH_ELRS_RAW'],
			unit: '°/sec',
			exact: false
		},
		GEN_THROTTLE_SETPOINT: {
			name: 'Throttle Setpoint',
			replaces: 'LOG_THROTTLE_SETPOINT',
			requires: ['LOG_THROTTLE_ELRS_RAW'],
			unit: '°/sec',
			exact: false
		},
		GEN_YAW_SETPOINT: {
			name: 'Yaw Setpoint',
			replaces: 'LOG_YAW_SETPOINT',
			requires: ['LOG_YAW_ELRS_RAW'],
			unit: '°/sec',
			exact: false
		},
		GEN_ROLL_PID_P: {
			name: 'Roll PID P',
			replaces: 'LOG_ROLL_PID_P',
			requires: [['LOG_ROLL_SETPOINT', 'GEN_ROLL_SETPOINT'], 'LOG_ROLL_GYRO_RAW'],
			unit: '',
			exact: true
		},
		GEN_ROLL_PID_I: {
			name: 'Roll PID I',
			replaces: 'LOG_ROLL_PID_I',
			requires: [['LOG_ROLL_SETPOINT', 'GEN_ROLL_SETPOINT'], 'LOG_ROLL_GYRO_RAW'],
			unit: '',
			exact: false
		},
		GEN_ROLL_PID_D: {
			name: 'Roll PID D',
			replaces: 'LOG_ROLL_PID_D',
			requires: ['LOG_ROLL_GYRO_RAW'],
			unit: '',
			exact: false
		},
		GEN_ROLL_PID_FF: {
			name: 'Roll PID FF',
			replaces: 'LOG_ROLL_PID_FF',
			requires: [['LOG_ROLL_SETPOINT', 'GEN_ROLL_SETPOINT']],
			unit: '',
			exact: false
		},
		GEN_ROLL_PID_S: {
			name: 'Roll PID S',
			replaces: 'LOG_ROLL_PID_S',
			requires: [['LOG_ROLL_SETPOINT', 'GEN_ROLL_SETPOINT']],
			unit: '',
			exact: true
		},
		GEN_PITCH_PID_P: {
			name: 'Pitch PID P',
			replaces: 'LOG_PITCH_PID_P',
			requires: [['LOG_PITCH_SETPOINT', 'GEN_PITCH_SETPOINT'], 'LOG_PITCH_GYRO_RAW'],
			unit: '',
			exact: true
		},
		GEN_PITCH_PID_I: {
			name: 'Pitch PID I',
			replaces: 'LOG_PITCH_PID_I',
			requires: [['LOG_PITCH_SETPOINT', 'GEN_PITCH_SETPOINT'], 'LOG_PITCH_GYRO_RAW'],
			unit: '',
			exact: false
		},
		GEN_PITCH_PID_D: {
			name: 'Pitch PID D',
			replaces: 'LOG_PITCH_PID_D',
			requires: ['LOG_PITCH_GYRO_RAW'],
			unit: '',
			exact: false
		},
		GEN_PITCH_PID_FF: {
			name: 'Pitch PID FF',
			replaces: 'LOG_PITCH_PID_FF',
			requires: [['LOG_PITCH_SETPOINT', 'GEN_PITCH_SETPOINT']],
			unit: '',
			exact: false
		},
		GEN_PITCH_PID_S: {
			name: 'Pitch PID S',
			replaces: 'LOG_PITCH_PID_S',
			requires: [['LOG_PITCH_SETPOINT', 'GEN_PITCH_SETPOINT']],
			unit: '',
			exact: true
		},
		GEN_YAW_PID_P: {
			name: 'Yaw PID P',
			replaces: 'LOG_YAW_PID_P',
			requires: [['LOG_YAW_SETPOINT', 'GEN_YAW_SETPOINT'], 'LOG_YAW_GYRO_RAW'],
			unit: '',
			exact: true
		},
		GEN_YAW_PID_I: {
			name: 'Yaw PID I',
			replaces: 'LOG_YAW_PID_I',
			requires: [['LOG_YAW_SETPOINT', 'GEN_YAW_SETPOINT'], 'LOG_YAW_GYRO_RAW'],
			unit: '',
			exact: false
		},
		GEN_YAW_PID_D: {
			name: 'Yaw PID D',
			replaces: 'LOG_YAW_PID_D',
			requires: ['LOG_YAW_GYRO_RAW'],
			unit: '',
			exact: false
		},
		GEN_YAW_PID_FF: {
			name: 'Yaw PID FF',
			replaces: 'LOG_YAW_PID_FF',
			requires: [['LOG_YAW_SETPOINT', 'GEN_YAW_SETPOINT']],
			unit: '',
			exact: false
		},
		GEN_YAW_PID_S: {
			name: 'Yaw PID S',
			replaces: 'LOG_YAW_PID_S',
			requires: [['LOG_YAW_SETPOINT', 'GEN_YAW_SETPOINT']],
			unit: '',
			exact: true
		},
		GEN_MOTOR_OUTPUTS: {
			name: 'Motor Outputs',
			replaces: 'LOG_MOTOR_OUTPUTS',
			requires: [
				['GEN_THROTTLE_SETPOINT', 'LOG_THROTTLE_SETPOINT'],
				['LOG_ROLL_PID_P', 'GEN_ROLL_PID_P'],
				['LOG_ROLL_PID_I', 'GEN_ROLL_PID_I'],
				['LOG_ROLL_PID_D', 'GEN_ROLL_PID_D'],
				['LOG_ROLL_PID_FF', 'GEN_ROLL_PID_FF'],
				['LOG_ROLL_PID_S', 'GEN_ROLL_PID_S'],
				['LOG_PITCH_PID_P', 'GEN_PITCH_PID_P'],
				['LOG_PITCH_PID_I', 'GEN_PITCH_PID_I'],
				['LOG_PITCH_PID_D', 'GEN_PITCH_PID_D'],
				['LOG_PITCH_PID_FF', 'GEN_PITCH_PID_FF'],
				['LOG_PITCH_PID_S', 'GEN_PITCH_PID_S'],
				['LOG_YAW_PID_P', 'GEN_YAW_PID_P'],
				['LOG_YAW_PID_I', 'GEN_YAW_PID_I'],
				['LOG_YAW_PID_D', 'GEN_YAW_PID_D'],
				['LOG_YAW_PID_FF', 'GEN_YAW_PID_FF'],
				['LOG_YAW_PID_S', 'GEN_YAW_PID_S']
			],
			unit: '',
			exact: true
		},
		GEN_REL_VVEL: {
			name: 'Rel. vVel',
			replaces: 'LOG_REL_VVEL',
			requires: ['LOG_VERTICAL_ACCEL'],
			unit: 'm/s',
			exact: false
		},
		GEN_REL_ALT: {
			name: 'Rel. Altitude',
			replaces: 'LOG_REL_ALT',
			requires: [['LOG_REL_VVEL', 'GEN_REL_VVEL']],
			unit: 'm',
			exact: false
		}
	} as {
		[key: string]: {
			name: string;
			replaces: string;
			requires: (string | string[])[]; // if its a string, that has to be in there. If its an array, one of the mentioned ones has to be in there
			unit: string;
			exact: boolean;
		};
	};

	function fillLogWithGenFlags(log: BBLog) {
		log.isExact = true;
		const genFlags = Object.keys(BB_GEN_FLAGS);
		for (let i = 0; i < genFlags.length; i++) {
			const flagName = genFlags[i];
			const flag = BB_GEN_FLAGS[flagName];
			if (log.flags.includes(flag.replaces)) continue;
			if (
				flag.requires.every((r) => {
					if (typeof r === 'string') return log.flags.includes(r);
					if (Array.isArray(r)) {
						for (const s of r) if (log.flags.includes(s)) return true;
						return false;
					}
					return false;
				})
			) {
				log.flags.push(flagName);
				//generate entries
				if (!flag.exact) log.isExact = false;
				const path = BB_ALL_FLAGS[flag.replaces].path;
				switch (flagName) {
					case 'GEN_ROLL_SETPOINT':
						log.frames.forEach((f) => {
							const polynomials: number[] = [(f.elrs.roll! - 1500) / 512];
							for (let i = 1; i < 5; i++) {
								polynomials[i] = polynomials[0] * polynomials[i - 1];
								if (polynomials[0] < 0) polynomials[i] = -polynomials[i];
							}
							f.setpoint.roll = 0;
							for (let i = 0; i < 5; i++)
								f.setpoint.roll! += polynomials[i] * log.rateFactors[i][0];
						});
						break;
					case 'GEN_PITCH_SETPOINT':
						log.frames.forEach((f) => {
							const polynomials: number[] = [(f.elrs.pitch! - 1500) / 512];
							for (let i = 1; i < 5; i++) {
								polynomials[i] = polynomials[0] * polynomials[i - 1];
								if (polynomials[0] < 0) polynomials[i] = -polynomials[i];
							}
							f.setpoint.pitch = 0;
							for (let i = 0; i < 5; i++)
								f.setpoint.pitch! += polynomials[i] * log.rateFactors[i][1];
						});
						break;
					case 'GEN_THROTTLE_SETPOINT':
						log.frames.forEach((f) => {
							f.setpoint.throttle = f.elrs.throttle;
						});
						break;
					case 'GEN_YAW_SETPOINT':
						log.frames.forEach((f) => {
							const polynomials: number[] = [(f.elrs.yaw! - 1500) / 512];
							for (let i = 1; i < 5; i++) {
								polynomials[i] = polynomials[0] * polynomials[i - 1];
								if (polynomials[0] < 0) polynomials[i] = -polynomials[i];
							}
							f.setpoint.yaw = 0;
							for (let i = 0; i < 5; i++) f.setpoint.yaw! += polynomials[i] * log.rateFactors[i][2];
						});
						break;
					case 'GEN_ROLL_PID_P':
						log.frames.forEach((f) => {
							f.pid.roll.p = (f.setpoint.roll! - f.gyro.roll!) * log.pidConstants[0][0];
						});
						break;
					case 'GEN_PITCH_PID_P':
						log.frames.forEach((f) => {
							f.pid.pitch.p = (f.setpoint.pitch! - f.gyro.pitch!) * log.pidConstants[1][0];
						});
						break;
					case 'GEN_YAW_PID_P':
						log.frames.forEach((f) => {
							f.pid.yaw.p = (f.setpoint.yaw! - f.gyro.yaw!) * log.pidConstants[2][0];
						});
						break;
					case 'GEN_ROLL_PID_D':
						log.frames.forEach((f, i) => {
							f.pid.roll.d =
								((f.gyro.roll! - (log.frames[i - 1]?.gyro.roll || 0)) * log.pidConstants[0][2]) /
								log.frequencyDivider;
						});
						break;
					case 'GEN_PITCH_PID_D':
						log.frames.forEach((f, i) => {
							f.pid.pitch.d =
								((f.gyro.pitch! - (log.frames[i - 1]?.gyro.pitch || 0)) * log.pidConstants[1][2]) /
								log.frequencyDivider;
						});
						break;
					case 'GEN_YAW_PID_D':
						log.frames.forEach((f, i) => {
							f.pid.yaw.d =
								((f.gyro.yaw! - (log.frames[i - 1]?.gyro.yaw || 0)) * log.pidConstants[2][2]) /
								log.frequencyDivider;
						});
						break;
					case 'GEN_ROLL_PID_FF':
						{
							const step = Math.round(8 / log.frequencyDivider) || 1;
							const divider = (step * log.frequencyDivider) / 8;
							log.frames.forEach((f, i) => {
								f.pid.roll.ff =
									((f.setpoint.roll! - log.frames[i - step]?.setpoint.roll! || 0) *
										log.pidConstants[0][3]) /
									divider;
							});
						}
						break;
					case 'GEN_PITCH_PID_FF':
						{
							const step = Math.round(8 / log.frequencyDivider) || 1;
							const divider = step * log.frequencyDivider;
							log.frames.forEach((f, i) => {
								f.pid.pitch.ff =
									((f.setpoint.pitch! - log.frames[i - step]?.setpoint.pitch! || 0) *
										log.pidConstants[1][3]) /
									divider;
							});
						}
						break;
					case 'GEN_YAW_PID_FF':
						{
							const step = Math.round(8 / log.frequencyDivider) || 1;
							const divider = step * log.frequencyDivider;
							log.frames.forEach((f, i) => {
								f.pid.yaw.ff =
									((f.setpoint.yaw! - log.frames[i - step]?.setpoint.yaw! || 0) *
										log.pidConstants[2][3]) /
									divider;
							});
						}
						break;
					case 'GEN_ROLL_PID_S':
						log.frames.forEach((f) => {
							f.pid.roll.s = f.setpoint.roll! * log.pidConstants[0][4];
						});
						break;
					case 'GEN_PITCH_PID_S':
						log.frames.forEach((f) => {
							f.pid.pitch.s = f.setpoint.pitch! * log.pidConstants[1][4];
						});
						break;
					case 'GEN_YAW_PID_S':
						log.frames.forEach((f) => {
							f.pid.yaw.s = f.setpoint.yaw! * log.pidConstants[2][4];
						});
						break;
					case 'GEN_ROLL_PID_I':
						{
							let rollI = 0;
							let takeoffCounter = 0;
							log.frames.forEach((f) => {
								rollI += f.setpoint.roll! - f.gyro.roll!;
								if (f.setpoint.throttle! > 1020) takeoffCounter += log.frequencyDivider;
								else if (takeoffCounter < 1000) takeoffCounter = 0;
								if (takeoffCounter < 1000)
									rollI *= Math.pow(log.pidConstants[0][6], log.frequencyDivider);
								f.pid.roll.i = rollI * log.pidConstants[0][1];
							});
						}
						break;
					case 'GEN_PITCH_PID_I':
						{
							let pitchI = 0;
							let takeoffCounter = 0;
							log.frames.forEach((f) => {
								pitchI += f.setpoint.pitch! - f.gyro.pitch!;
								if (f.setpoint.throttle! > 1020) takeoffCounter += log.frequencyDivider;
								else if (takeoffCounter < 1000) takeoffCounter = 0;
								if (takeoffCounter < 1000)
									pitchI *= Math.pow(log.pidConstants[1][6], log.frequencyDivider);
								f.pid.pitch.i = pitchI * log.pidConstants[1][1];
							});
						}
						break;
					case 'GEN_YAW_PID_I':
						{
							let yawI = 0;
							let takeoffCounter = 0;
							log.frames.forEach((f) => {
								yawI += f.setpoint.yaw! - f.gyro.yaw!;
								if (f.setpoint.throttle! > 1020) takeoffCounter += log.frequencyDivider;
								else if (takeoffCounter < 1000) takeoffCounter = 0;
								if (takeoffCounter < 1000)
									yawI *= Math.pow(log.pidConstants[2][6], log.frequencyDivider);
								f.pid.yaw.i = yawI * log.pidConstants[2][1];
							});
						}
						break;
					case 'GEN_MOTOR_OUTPUTS':
						log.frames.forEach((f) => {
							const rollTerm =
								f.pid.roll.p! + f.pid.roll.i! + f.pid.roll.d! + f.pid.roll.ff! + f.pid.roll.s!;
							const pitchTerm =
								f.pid.pitch.p! + f.pid.pitch.i! + f.pid.pitch.d! + f.pid.pitch.ff! + f.pid.pitch.s!;
							const yawTerm =
								f.pid.yaw.p! + f.pid.yaw.i! + f.pid.yaw.d! + f.pid.yaw.ff! + f.pid.yaw.s!;
							f.motors.out = {
								rr: (f.setpoint.throttle! - 1000) * 2 - rollTerm + pitchTerm + yawTerm,
								fr: (f.setpoint.throttle! - 1000) * 2 - rollTerm - pitchTerm - yawTerm,
								rl: (f.setpoint.throttle! - 1000) * 2 + rollTerm + pitchTerm - yawTerm,
								fl: (f.setpoint.throttle! - 1000) * 2 + rollTerm - pitchTerm + yawTerm
							};
							f.motors.out.rr = map(f.motors.out.rr!, 0, 2000, 50, 2000);
							f.motors.out.fr = map(f.motors.out.fr!, 0, 2000, 50, 2000);
							f.motors.out.rl = map(f.motors.out.rl!, 0, 2000, 50, 2000);
							f.motors.out.fl = map(f.motors.out.fl!, 0, 2000, 50, 2000);
							if (f.motors.out.rr! > 2000) {
								const diff = 2000 - f.motors.out.rr!;
								f.motors.out.rr = 2000;
								f.motors.out.fr! -= diff;
								f.motors.out.rl! -= diff;
								f.motors.out.fl! -= diff;
							}
							if (f.motors.out.fr! > 2000) {
								const diff = 2000 - f.motors.out.fr!;
								f.motors.out.fr = 2000;
								f.motors.out.rr! -= diff;
								f.motors.out.rl! -= diff;
								f.motors.out.fl! -= diff;
							}
							if (f.motors.out.rl! > 2000) {
								const diff = 2000 - f.motors.out.rl!;
								f.motors.out.rl = 2000;
								f.motors.out.rr! -= diff;
								f.motors.out.fr! -= diff;
								f.motors.out.fl! -= diff;
							}
							if (f.motors.out.fl! > 2000) {
								const diff = 2000 - f.motors.out.fl!;
								f.motors.out.fl = 2000;
								f.motors.out.rr! -= diff;
								f.motors.out.fr! -= diff;
								f.motors.out.rl! -= diff;
							}
							if (f.motors.out.rr! < 50) {
								const diff = 50 - f.motors.out.rr!;
								f.motors.out.rr = 50;
								f.motors.out.fr! += diff;
								f.motors.out.rl! += diff;
								f.motors.out.fl! += diff;
							}
							if (f.motors.out.fr! < 50) {
								const diff = 50 - f.motors.out.fr!;
								f.motors.out.fr = 50;
								f.motors.out.rr! += diff;
								f.motors.out.rl! += diff;
								f.motors.out.fl! += diff;
							}
							if (f.motors.out.rl! < 50) {
								const diff = 50 - f.motors.out.rl!;
								f.motors.out.rl = 50;
								f.motors.out.rr! += diff;
								f.motors.out.fr! += diff;
								f.motors.out.fl! += diff;
							}
							if (f.motors.out.fl! < 50) {
								const diff = 50 - f.motors.out.fl!;
								f.motors.out.fl = 50;
								f.motors.out.rr! += diff;
								f.motors.out.fr! += diff;
								f.motors.out.rl! += diff;
							}
							f.motors.out.rr = Math.min(f.motors.out.rr!, 2000);
							f.motors.out.fr = Math.min(f.motors.out.fr!, 2000);
							f.motors.out.rl = Math.min(f.motors.out.rl!, 2000);
							f.motors.out.fl = Math.min(f.motors.out.fl!, 2000);
						});
						break;
					case 'GEN_REL_VVEL':
						log.frames.forEach((f, i) => {
							f.motion.relVVel =
								(f.motion.accelVertical! - 9.81) / log.framesPerSecond +
								(log.frames[i - 1]?.motion.relVVel || 0);
						});
					case 'GEN_REL_ALT':
						log.frames.forEach((f, i) => {
							f.motion.relAlt =
								f.motion.relVVel! / log.framesPerSecond + (log.frames[i - 1]?.motion.relAlt || 0);
						});
						break;
				}
			}
		}
	}
	function map(value: number, inMin: number, inMax: number, outMin: number, outMax: number) {
		return ((value - inMin) * (outMax - outMin)) / (inMax - inMin) + outMin;
	}

	$: handleCommand($port);

	function handleCommand(command: Command) {
		switch (command.command) {
			case ConfigCmd.BB_FILE_LIST | 0x4000:
				logNums = command.data.map((n) => ({ text: '', num: n }));
				if (!logNums.length) {
					logNums = [{ text: 'No logs found', num: -1 }];
					selected = -1;
					return;
				}
				selected = logNums[0].num;
				logInfoPosition = 0;
				//@ts-ignore
				logInfoInterval = setInterval(getLogInfo, 100);
				break;
			case ConfigCmd.BB_FILE_INFO | 0x4000:
				processLogInfo(command.data);
				break;
			case ConfigCmd.BB_FILE_DOWNLOAD | 0x4000:
				handleFileChunk(command.data);
				break;
			case ConfigCmd.CONFIGURATOR_PING | 0x4000:
			case ConfigCmd.STATUS | 0x4000:
				break;
			default:
				if (
					typeof command.command === 'number' &&
					!Number.isNaN(command.command) &&
					!Object.values(ConfigCmd).includes(command.command & 0x3fff)
				)
					console.log({ ...command });
		}
	}

	function handleFileChunk(data: number[]) {
		//1027 data bytes per packet
		//first byte is file number
		//second and third are chunk number
		//up to 1024 bytes of binary file data
		//last packet has the chunk number set to 0xFFFF, and then two bytes that indicate the number of total chunks
		if (binFileNumber !== data[0]) {
			binFileNumber = data[0];
			binFile = [];
			receivedChunks = [];
			graphs = [[]];
			loadedLog = undefined;
			totalChunks = -1;
		}
		const chunkNum = leBytesToInt(data.slice(1, 3));
		if (chunkNum === 0xffff) {
			console.log('final chunk');
			totalChunks = leBytesToInt(data.slice(3, 5));
		} else {
			const chunkSize = data.length - 3;
			const chunk = data.slice(3, 3 + chunkSize);
			receivedChunks[chunkNum] = true;
			for (let i = chunkNum * 1024; i < chunkNum * 1024 + chunkSize; i++) {
				binFile[i] = chunk[i - chunkNum * 1024];
			}
		}
		if (receivedChunks.length === totalChunks) {
			for (let i = 0; i < totalChunks; i++) {
				if (!receivedChunks[i]) {
					console.log('Missing chunk: ' + i);
					port.sendCommand(ConfigCmd.BB_FILE_DOWNLOAD, [binFileNumber, i & 0xff, (i >> 8) & 0xff]);
					return;
				}
			}
			decodeBinFile();
		}
	}

	function decodeBinFile() {
		const header = binFile.slice(0, 256);
		const data = binFile.slice(256);
		const magic = leBytesToInt(header.slice(0, 4));
		if (magic !== 0x99a12720) {
			rejectWrongFile(
				'Wrong magic number: 0x' +
					magic.toString(16) +
					' instead of 0x99a12720, receivedChunks.length: ' +
					receivedChunks.length +
					', totalChunks: ' +
					totalChunks
			);
			return;
		}
		const version = header.slice(4, 7);
		const sTime = leBytesToInt(header.slice(7, 11));
		const year = sTime >> 26;
		const month = (sTime >> 22) & 0b1111;
		const day = (sTime >> 17) & 0b11111;
		const hour = (sTime >> 12) & 0b11111;
		const minute = (sTime >> 6) & 0b111111;
		const second = sTime & 0b111111;
		const startTime = new Date(year + 2020, month - 1, day, hour, minute, second);
		const pidFreq = 3200 / (1 + header[11]);
		const freqDiv = header[12];
		const rangeByte = header[13];
		const ranges = {
			gyro: GYRO_RANGES[(rangeByte >> 2) & 0b111],
			accel: ACC_RANGES[rangeByte & 0b11]
		};
		const rateFactors: number[][] = [[], [], [], [], []];
		const rfBytes = header.slice(14, 74);
		for (let i = 0; i < 5; i++)
			for (let j = 0; j < 3; j++)
				rateFactors[i][j] = leBytesToInt(rfBytes.slice(i * 12 + j * 4, i * 12 + j * 4 + 4)) / 65536;
		const pidConstants: number[][] = [[], [], []];
		const pidConstantsNice: number[][] = [[], [], []];
		const pcBytes = header.slice(74, 158);
		for (let i = 0; i < 3; i++) {
			pidConstants[i][0] = leBytesToInt(pcBytes.slice(i * 28, i * 28 + 4));
			pidConstantsNice[i][0] = pidConstants[i][0] >> 11;
			pidConstants[i][0] /= 65536;
			pidConstants[i][1] = leBytesToInt(pcBytes.slice(i * 28 + 4, i * 28 + 8));
			pidConstantsNice[i][1] = pidConstants[i][1] >> 3;
			pidConstants[i][1] /= 65536;
			pidConstants[i][2] = leBytesToInt(pcBytes.slice(i * 28 + 8, i * 28 + 12));
			pidConstantsNice[i][2] = pidConstants[i][2] >> 10;
			pidConstants[i][2] /= 65536;
			pidConstants[i][3] = leBytesToInt(pcBytes.slice(i * 28 + 12, i * 28 + 16));
			pidConstantsNice[i][3] = pidConstants[i][3] >> 13;
			pidConstants[i][3] /= 65536;
			pidConstants[i][4] = leBytesToInt(pcBytes.slice(i * 28 + 16, i * 28 + 20));
			pidConstantsNice[i][4] = pidConstants[i][4] >> 8;
			pidConstants[i][4] /= 65536;
			for (let j = 5; j < 7; j++)
				pidConstants[i][j] =
					leBytesToInt(pcBytes.slice(i * 28 + j * 4, i * 28 + j * 4 + 4)) / 65536;
		}
		const flagsLow = leBytesToInt(header.slice(158, 162));
		const flagsHigh = leBytesToInt(header.slice(162, 166));
		const motorPoles = header[166];
		const flags: string[] = [];
		let frameSize = 0;
		const offsets: { [key: string]: number } = {};
		for (let i = 0; i < 32 && Object.keys(BB_ALL_FLAGS).length > i; i++) {
			const flagIsSet = flagsLow & (1 << i);
			if (flagIsSet) {
				flags.push(Object.keys(BB_ALL_FLAGS)[i]);
				offsets[Object.keys(BB_ALL_FLAGS)[i]] = frameSize;
				if (i == 26) frameSize += 6;
				else if (i == 28) frameSize++;
				else frameSize += 2;
			}
		}
		for (let i = 0; i < 32 && Object.keys(BB_ALL_FLAGS).length > i + 32; i++) {
			const flagIsSet = flagsHigh & (1 << i);
			if (flagIsSet) {
				flags.push(Object.keys(BB_ALL_FLAGS)[i + 32]);
				offsets[Object.keys(BB_ALL_FLAGS)[i + 32]] = frameSize;
				if (i + 32 === 35 || i + 32 === 36) frameSize += 6;
				else frameSize += 2;
			}
		}
		const framesPerSecond = pidFreq / freqDiv;
		const frames = data.length / frameSize;
		const log: LogFrame[] = [];
		for (let i = 0; i < data.length; i += frameSize) {
			const frame: LogFrame = {
				elrs: {},
				setpoint: {},
				gyro: {},
				pid: { roll: {}, pitch: {}, yaw: {} },
				motors: { out: {}, rpm: {} },
				motion: { gps: {}, accelRaw: {} },
				attitude: {}
			};
			if (flags.includes('LOG_ROLL_ELRS_RAW'))
				frame.elrs.roll = leBytesToInt(
					data.slice(i + offsets['LOG_ROLL_ELRS_RAW'], i + offsets['LOG_ROLL_ELRS_RAW'] + 2)
				);
			if (flags.includes('LOG_PITCH_ELRS_RAW'))
				frame.elrs.pitch = leBytesToInt(
					data.slice(i + offsets['LOG_PITCH_ELRS_RAW'], i + offsets['LOG_PITCH_ELRS_RAW'] + 2)
				);
			if (flags.includes('LOG_THROTTLE_ELRS_RAW'))
				frame.elrs.throttle = leBytesToInt(
					data.slice(i + offsets['LOG_THROTTLE_ELRS_RAW'], i + offsets['LOG_THROTTLE_ELRS_RAW'] + 2)
				);
			if (flags.includes('LOG_YAW_ELRS_RAW'))
				frame.elrs.yaw = leBytesToInt(
					data.slice(i + offsets['LOG_YAW_ELRS_RAW'], i + offsets['LOG_YAW_ELRS_RAW'] + 2)
				);
			if (flags.includes('LOG_ROLL_SETPOINT'))
				frame.setpoint.roll =
					leBytesToInt(
						data.slice(i + offsets['LOG_ROLL_SETPOINT'], i + offsets['LOG_ROLL_SETPOINT'] + 2),
						true
					) / 16; // data is 12.4 fixed point
			if (flags.includes('LOG_PITCH_SETPOINT'))
				frame.setpoint.pitch =
					leBytesToInt(
						data.slice(i + offsets['LOG_PITCH_SETPOINT'], i + offsets['LOG_PITCH_SETPOINT'] + 2),
						true
					) / 16;
			if (flags.includes('LOG_THROTTLE_SETPOINT'))
				frame.setpoint.throttle =
					leBytesToInt(
						data.slice(
							i + offsets['LOG_THROTTLE_SETPOINT'],
							i + offsets['LOG_THROTTLE_SETPOINT'] + 2
						)
					) /
						32 +
					1000;
			if (flags.includes('LOG_YAW_SETPOINT'))
				frame.setpoint.yaw =
					leBytesToInt(
						data.slice(i + offsets['LOG_YAW_SETPOINT'], i + offsets['LOG_YAW_SETPOINT'] + 2),
						true
					) / 16;
			if (flags.includes('LOG_ROLL_GYRO_RAW'))
				frame.gyro.roll =
					leBytesToInt(
						data.slice(i + offsets['LOG_ROLL_GYRO_RAW'], i + offsets['LOG_ROLL_GYRO_RAW'] + 2),
						true
					) / 16;
			if (flags.includes('LOG_PITCH_GYRO_RAW'))
				frame.gyro.pitch =
					leBytesToInt(
						data.slice(i + offsets['LOG_PITCH_GYRO_RAW'], i + offsets['LOG_PITCH_GYRO_RAW'] + 2),
						true
					) / 16;
			if (flags.includes('LOG_YAW_GYRO_RAW'))
				frame.gyro.yaw =
					leBytesToInt(
						data.slice(i + offsets['LOG_YAW_GYRO_RAW'], i + offsets['LOG_YAW_GYRO_RAW'] + 2),
						true
					) / 16;
			if (flags.includes('LOG_ROLL_PID_P'))
				frame.pid.roll.p = leBytesToInt(
					data.slice(i + offsets['LOG_ROLL_PID_P'], i + offsets['LOG_ROLL_PID_P'] + 2),
					true
				);
			if (flags.includes('LOG_ROLL_PID_I'))
				frame.pid.roll.i = leBytesToInt(
					data.slice(i + offsets['LOG_ROLL_PID_I'], i + offsets['LOG_ROLL_PID_I'] + 2),
					true
				);
			if (flags.includes('LOG_ROLL_PID_D'))
				frame.pid.roll.d = leBytesToInt(
					data.slice(i + offsets['LOG_ROLL_PID_D'], i + offsets['LOG_ROLL_PID_D'] + 2),
					true
				);
			if (flags.includes('LOG_ROLL_PID_FF'))
				frame.pid.roll.ff = leBytesToInt(
					data.slice(i + offsets['LOG_ROLL_PID_FF'], i + offsets['LOG_ROLL_PID_FF'] + 2),
					true
				);
			if (flags.includes('LOG_ROLL_PID_S'))
				frame.pid.roll.s = leBytesToInt(
					data.slice(i + offsets['LOG_ROLL_PID_S'], i + offsets['LOG_ROLL_PID_S'] + 2),
					true
				);
			if (flags.includes('LOG_PITCH_PID_P'))
				frame.pid.pitch.p = leBytesToInt(
					data.slice(i + offsets['LOG_PITCH_PID_P'], i + offsets['LOG_PITCH_PID_P'] + 2),
					true
				);
			if (flags.includes('LOG_PITCH_PID_I'))
				frame.pid.pitch.i = leBytesToInt(
					data.slice(i + offsets['LOG_PITCH_PID_I'], i + offsets['LOG_PITCH_PID_I'] + 2),
					true
				);
			if (flags.includes('LOG_PITCH_PID_D'))
				frame.pid.pitch.d = leBytesToInt(
					data.slice(i + offsets['LOG_PITCH_PID_D'], i + offsets['LOG_PITCH_PID_D'] + 2),
					true
				);
			if (flags.includes('LOG_PITCH_PID_FF'))
				frame.pid.pitch.ff = leBytesToInt(
					data.slice(i + offsets['LOG_PITCH_PID_FF'], i + offsets['LOG_PITCH_PID_FF'] + 2),
					true
				);
			if (flags.includes('LOG_PITCH_PID_S'))
				frame.pid.pitch.s = leBytesToInt(
					data.slice(i + offsets['LOG_PITCH_PID_S'], i + offsets['LOG_PITCH_PID_S'] + 2),
					true
				);
			if (flags.includes('LOG_YAW_PID_P'))
				frame.pid.yaw.p = leBytesToInt(
					data.slice(i + offsets['LOG_YAW_PID_P'], i + offsets['LOG_YAW_PID_P'] + 2),
					true
				);
			if (flags.includes('LOG_YAW_PID_I'))
				frame.pid.yaw.i = leBytesToInt(
					data.slice(i + offsets['LOG_YAW_PID_I'], i + offsets['LOG_YAW_PID_I'] + 2),
					true
				);
			if (flags.includes('LOG_YAW_PID_D'))
				frame.pid.yaw.d = leBytesToInt(
					data.slice(i + offsets['LOG_YAW_PID_D'], i + offsets['LOG_YAW_PID_D'] + 2),
					true
				);
			if (flags.includes('LOG_YAW_PID_FF'))
				frame.pid.yaw.ff = leBytesToInt(
					data.slice(i + offsets['LOG_YAW_PID_FF'], i + offsets['LOG_YAW_PID_FF'] + 2),
					true
				);
			if (flags.includes('LOG_YAW_PID_S'))
				frame.pid.yaw.s = leBytesToInt(
					data.slice(i + offsets['LOG_YAW_PID_S'], i + offsets['LOG_YAW_PID_S'] + 2),
					true
				);
			if (flags.includes('LOG_MOTOR_OUTPUTS')) {
				const throttleBytes = data.slice(
					i + offsets['LOG_MOTOR_OUTPUTS'],
					i + offsets['LOG_MOTOR_OUTPUTS'] + 6
				);
				const motors01 = leBytesToInt(throttleBytes.slice(0, 3));
				const motors23 = leBytesToInt(throttleBytes.slice(3, 6));
				frame.motors.out.rr = motors01 & 0xfff;
				frame.motors.out.fr = motors01 >> 12;
				frame.motors.out.rl = motors23 & 0xfff;
				frame.motors.out.fl = motors23 >> 12;
			}
			if (flags.includes('LOG_FRAMETIME'))
				frame.frametime = leBytesToInt(
					data.slice(i + offsets['LOG_FRAMETIME'], i + offsets['LOG_FRAMETIME'] + 2)
				);
			if (flags.includes('LOG_FLIGHT_MODE')) {
				frame.flightMode = data[i + offsets['LOG_FLIGHT_MODE']];
			}
			if (flags.includes('LOG_ALTITUDE'))
				//12.4 fixed point
				frame.motion.altitude =
					leBytesToInt(
						data.slice(i + offsets['LOG_ALTITUDE'], i + offsets['LOG_ALTITUDE'] + 2),
						true
					) / 16;
			if (flags.includes('LOG_VVEL')) {
				//10.6 fixed point
				frame.motion.vvel =
					leBytesToInt(data.slice(i + offsets['LOG_VVEL'], i + offsets['LOG_VVEL'] + 2), true) / 64;
			}
			if (flags.includes('LOG_GPS')) {
				frame.motion.gps = log[log.length - 1]?.motion.gps || {};
				if (
					leBytesToInt(data.slice(i + offsets['LOG_GPS'], i + offsets['LOG_GPS'] + 2)) === 20551 && // 'G' * 256 + 'P'
					leBytesToInt(
						data.slice(i + offsets['LOG_GPS'] + frameSize, i + offsets['LOG_GPS'] + 2 + frameSize)
					) === 20563 && // 'G' * 256 + 'P'
					leBytesToInt(
						data.slice(
							i + offsets['LOG_GPS'] + frameSize * 2,
							i + offsets['LOG_GPS'] + 2 + frameSize * 2
						)
					) === 21590 // 'V' * 256 + 'T'
				) {
					const gpsData = [] as number[];
					//copy 92 bytes of GPS data from the next 46 frames (after the 3 frames for GPSPVT)
					for (let j = 0; j < 46; j++) {
						const gpsBytes = data.slice(
							i + offsets['LOG_GPS'] + frameSize * 3 + j * frameSize,
							i + offsets['LOG_GPS'] + frameSize * 3 + j * frameSize + 2
						);
						gpsData.push(...gpsBytes);
					}
					frame.motion.gps = {
						year: leBytesToInt(gpsData.slice(4, 6)),
						month: gpsData[6],
						day: gpsData[7],
						hour: gpsData[8],
						minute: gpsData[9],
						second: gpsData[10],
						time_validity_flags: gpsData[11],
						t_acc: leBytesToInt(gpsData.slice(12, 16)),
						ns: leBytesToInt(gpsData.slice(16, 20), true),
						fix_type: gpsData[20],
						flags: gpsData[21],
						flags2: gpsData[22],
						sat_count: gpsData[23],
						lon: leBytesToInt(gpsData.slice(24, 28), true) / 10000000,
						lat: leBytesToInt(gpsData.slice(28, 32), true) / 10000000,
						alt: leBytesToInt(gpsData.slice(36, 40), true) / 1000,
						h_acc: leBytesToInt(gpsData.slice(40, 44)) / 1000,
						v_acc: leBytesToInt(gpsData.slice(44, 48)) / 1000,
						vel_n: leBytesToInt(gpsData.slice(48, 52), true) / 1000,
						vel_e: leBytesToInt(gpsData.slice(52, 56), true) / 1000,
						vel_d: leBytesToInt(gpsData.slice(56, 60), true) / 1000,
						g_speed: leBytesToInt(gpsData.slice(60, 64), true) / 1000,
						head_mot: leBytesToInt(gpsData.slice(64, 68), true) / 100000,
						s_acc: leBytesToInt(gpsData.slice(68, 72)) / 1000,
						head_acc: leBytesToInt(gpsData.slice(72, 76)) / 100000,
						p_dop: leBytesToInt(gpsData.slice(76, 78)) / 100,
						flags3: leBytesToInt(gpsData.slice(78, 80))
					};
				}
			}
			if (flags.includes('LOG_ATT_ROLL')) {
				// roll = 0.0001 * raw (signed 16 bit int)
				frame.attitude.roll =
					leBytesToInt(
						data.slice(i + offsets['LOG_ATT_ROLL'], i + offsets['LOG_ATT_ROLL'] + 2),
						true
					) / 10000;
			}
			if (flags.includes('LOG_ATT_PITCH')) {
				// pitch = 0.0001 * raw (signed 16 bit int)
				frame.attitude.pitch =
					leBytesToInt(
						data.slice(i + offsets['LOG_ATT_PITCH'], i + offsets['LOG_ATT_PITCH'] + 2),
						true
					) / 10000;
			}
			if (flags.includes('LOG_ATT_YAW')) {
				// yaw = 0.0001 * raw (signed 16 bit int)
				frame.attitude.yaw =
					leBytesToInt(
						data.slice(i + offsets['LOG_ATT_YAW'], i + offsets['LOG_ATT_YAW'] + 2),
						true
					) / 10000;
			}
			if (flags.includes('LOG_MOTOR_RPM')) {
				const rpmBytes = data.slice(i + offsets['LOG_MOTOR_RPM'], i + offsets['LOG_MOTOR_RPM'] + 6);
				const motors01 = leBytesToInt(rpmBytes.slice(0, 3));
				const motors23 = leBytesToInt(rpmBytes.slice(3, 6));
				let rr = motors01 & 0xfff;
				let fr = motors01 >> 12;
				let rl = motors23 & 0xfff;
				let fl = motors23 >> 12;
				if (rr === 0xfff) {
					frame.motors.rpm.rr = 0;
				} else {
					rr = (rr & 0x1ff) << (rr >> 9);
					frame.motors.rpm.rr = (60000000 + 50 * rr) / rr / (motorPoles / 2);
				}
				if (fr === 0xfff) {
					frame.motors.rpm.fr = 0;
				} else {
					fr = (fr & 0x1ff) << (fr >> 9);
					frame.motors.rpm.fr = (60000000 + 50 * fr) / fr / (motorPoles / 2);
				}
				if (rl === 0xfff) {
					frame.motors.rpm.rl = 0;
				} else {
					rl = (rl & 0x1ff) << (rl >> 9);
					frame.motors.rpm.rl = (60000000 + 50 * rl) / rl / (motorPoles / 2);
				}
				if (fl === 0xfff) {
					frame.motors.rpm.fl = 0;
				} else {
					fl = (fl & 0x1ff) << (fl >> 9);
					frame.motors.rpm.fl = (60000000 + 50 * fl) / fl / (motorPoles / 2);
				}
			}
			if (flags.includes('LOG_ACCEL_RAW')) {
				const accelBytes = data.slice(
					i + offsets['LOG_ACCEL_RAW'],
					i + offsets['LOG_ACCEL_RAW'] + 6
				);
				frame.motion.accelRaw.x = (leBytesToInt(accelBytes.slice(0, 2), true) * 9.81) / 2048;
				frame.motion.accelRaw.y = (leBytesToInt(accelBytes.slice(2, 4), true) * 9.81) / 2048;
				frame.motion.accelRaw.z = (leBytesToInt(accelBytes.slice(4, 6), true) * 9.81) / 2048;
			}
			if (flags.includes('LOG_VERTICAL_ACCEL')) {
				frame.motion.accelVertical =
					leBytesToInt(
						data.slice(i + offsets['LOG_VERTICAL_ACCEL'], i + offsets['LOG_VERTICAL_ACCEL'] + 2),
						true
					) / 128;
			}
			log.push(frame);
		}
		loadedLog = {
			frameCount: frames,
			flags,
			frames: log,
			version,
			startTime,
			ranges,
			pidFrequency: pidFreq,
			frequencyDivider: freqDiv,
			rateFactors,
			pidConstants,
			framesPerSecond,
			rawFile: binFile,
			isExact: true,
			pidConstantsNice,
			motorPoles
		};
		fillLogWithGenFlags(loadedLog);
		resolveWhenReady(loadedLog);
	}

	let logInfoPosition = 0;
	let logInfoInterval: number = -1;
	let selected = -1;
	function getLogInfo() {
		const infoNums = [] as number[];
		for (let i = 0; i < 10; i++) {
			if (logInfoPosition >= logNums.length) {
				clearInterval(logInfoInterval);
				break;
			}
			infoNums[i] = logNums[logInfoPosition++].num;
		}
		if (infoNums.length == 0) return;
		let checksum = 0;
		for (let i = 0; i < infoNums.length; i++) checksum ^= infoNums[i];
		checksum ^= 0x06;
		checksum ^= infoNums.length;
		port.sendCommand(0x06, infoNums);
	}

	function processLogInfo(data: number[]) {
		/* data of response (repeat 22 bytes for each log file)
		 * 0. file number
		 * 1-4. file size in bytes
		 * 5-7. version of bb file format
		 * 8-11: time of recording start
		 * 12. byte that indicates PID frequency
		 * 13. byte that indicates frequency divider
		 * 14-21: recording flags
		 */
		for (let i = 0; i < data.length; i += 22) {
			const fileNum = data[i];
			const fileSize =
				data[i + 1] + data[i + 2] * 256 + data[i + 3] * 256 * 256 + data[i + 4] * 256 * 256 * 256;
			const bbVersion = data[i + 5] * 256 * 256 + data[i + 6] * 256 + data[i + 7];
			const sTime = leBytesToInt(data.slice(i + 8, i + 12));
			const year = sTime >> 26;
			const month = (sTime >> 22) & 0b1111;
			const day = (sTime >> 17) & 0b11111;
			const hour = (sTime >> 12) & 0b11111;
			const minute = (sTime >> 6) & 0b111111;
			const second = sTime & 0b111111;
			const startTime = new Date(year + 2020, month - 1, day, hour, minute, second);
			const pidFreq = 3200 / (data[i + 12] + 1);
			const freqDiv = data[i + 13];
			const flags = data.slice(i + 14, i + 22);
			if (bbVersion !== 1) continue;
			const framesPerSecond = pidFreq / freqDiv;
			const dataBytes = fileSize - 256;
			let frameSize = 0;
			for (let j = 0; j < 64; j++) {
				//check flags
				// flag 26 (motors) has 6 bytes per frame, all others only 2
				// flag 28 (flight mode) has 1 byte per frame
				const byteNum = Math.floor(j / 8);
				const bitNum = j % 8;
				const flagIsSet = flags[byteNum] & (1 << bitNum);
				if (!flagIsSet) continue;
				if (j == 26 || j == 35) frameSize += 6;
				else if (j == 28) frameSize++;
				else frameSize += 2;
			}
			const frames = dataBytes / frameSize;
			//append duration of log file to logNums
			const index = logNums.findIndex((n) => n.num == fileNum);
			if (index == -1) continue;
			const duration = Math.round(frames / framesPerSecond);
			logNums[index].text = `${logNums[index].num} - ${duration}s - ${startTime.toLocaleString()}`;
			selected = fileNum;
		}
	}

	const canvas = document.createElement('canvas');
	let sliceAndSkip = [] as LogFrame[];
	let skipValue = 0;
	function drawCanvas(allowShortening = true) {
		if (!mounted) return;
		if (allowShortening) {
			clearTimeout(drawFullCanvasTimeout);
			drawFullCanvasTimeout = setTimeout(() => drawCanvas(false), 250);
		}
		const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		canvas.width = domCanvas.width;
		canvas.height = domCanvas.height;
		const ctx = canvas.getContext('2d') as CanvasRenderingContext2D;
		/**
		 * the drawing canvas has several graphs in it (in one column)
		 * each graph has a set of traces
		 * the whole drawing board has 1% of the height of free space on the top and bottom, as well as 2% oh the height of free space between each graph, no free space left and right
		 * each trace has a range, which represents the top and bottom on the graph for that trace
		 * a modifier appears for some flags, like motor outputs to define one specific motor for example
		 */
		const height = dataViewer.clientHeight * 0.98; //1% free space top and bottom
		const width = dataViewer.clientWidth;
		sliceAndSkip = dataSlice;
		if (!sliceAndSkip.length) return;
		skipValue = 1;
		const everyNth = Math.floor(sliceAndSkip.length / width);
		if (everyNth > 2 && allowShortening) {
			skipValue = everyNth;
			const len = sliceAndSkip.length;
			sliceAndSkip = [] as LogFrame[];
			for (let i = 0; i < len; i += everyNth) {
				sliceAndSkip.push(dataSlice[i]);
			}
		}
		ctx.clearRect(0, 0, dataViewer.clientWidth, dataViewer.clientHeight);
		const frameWidth = width / (sliceAndSkip.length - 1);
		const numGraphs = graphs.length;
		const heightPerGraph = (height - dataViewer.clientHeight * 0.02 * (numGraphs - 1)) / numGraphs;
		let heightOffset = 0.01 * dataViewer.clientHeight;
		for (let i = 0; i < numGraphs; i++) {
			ctx.strokeStyle = 'rgba(255, 255, 255, 0.5)';
			ctx.lineWidth = 1;
			ctx.beginPath();
			ctx.moveTo(0, heightOffset);
			ctx.lineTo(width, heightOffset);
			ctx.stroke();
			ctx.beginPath();
			ctx.moveTo(0, heightOffset + heightPerGraph);
			ctx.lineTo(width, heightOffset + heightPerGraph);
			ctx.stroke();
			ctx.beginPath();
			ctx.moveTo(0, heightOffset + heightPerGraph / 2);
			ctx.lineTo(width, heightOffset + heightPerGraph / 2);
			ctx.stroke();

			const graph = graphs[i];
			const numTraces = graph.length;
			for (let j = 0; j < numTraces; j++) {
				const trace = graph[j];
				const range = trace.maxValue - trace.minValue;
				const scale = heightPerGraph / range;
				ctx.strokeStyle = trace.color;
				ctx.lineWidth = trace.strokeWidth;
				let bbFlag = BB_ALL_FLAGS[trace.flagName];
				if (trace.flagName.startsWith('GEN_'))
					bbFlag = BB_ALL_FLAGS[BB_GEN_FLAGS[trace.flagName].replaces];
				let path = bbFlag?.path || '';
				if (bbFlag?.usesModifier) {
					if (trace.modifier) path += '.' + trace.modifier.toLowerCase();
					else continue;
				}
				ctx.beginPath();
				let pointY =
					heightOffset +
					heightPerGraph -
					(getNestedProperty(sliceAndSkip[0], path, {
						max: Math.max(trace.maxValue, trace.minValue),
						min: Math.min(trace.minValue, trace.maxValue)
					}) -
						trace.minValue) *
						scale;
				ctx.moveTo(0, pointY);
				for (let k = 1; k < sliceAndSkip.length; k++) {
					pointY =
						heightOffset +
						heightPerGraph -
						(getNestedProperty(sliceAndSkip[k], path, {
							max: Math.max(trace.maxValue, trace.minValue),
							min: Math.min(trace.minValue, trace.maxValue)
						}) -
							trace.minValue) *
							scale;
					ctx.lineTo(k * frameWidth, pointY);
				}
				ctx.stroke();
			}
			heightOffset += heightPerGraph + 0.02 * dataViewer.clientHeight;
		}
		domCanvas.getContext('2d')?.clearRect(0, 0, dataViewer.clientWidth, dataViewer.clientHeight);
		domCanvas.getContext('2d')?.drawImage(canvas, 0, 0);
	}
	let trackingStartX = -1;
	let trackingEndX = 0;
	let firstX = 0;
	let startStartFrame = 0;
	let startEndFrame = 0;
	const selectionCanvas = document.createElement('canvas');
	function onMouseMove(e: MouseEvent) {
		if (!loadedLog) return;
		if (e.buttons !== 1) {
			onMouseUp();
			// highlight all points on the current frame
			const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
			const domCtx = domCanvas.getContext('2d') as CanvasRenderingContext2D;
			domCtx.clearRect(0, 0, domCanvas.width, domCanvas.height);
			domCtx.drawImage(canvas, 0, 0);
			const closestFrame =
				Math.round(e.offsetX / (domCanvas.width / (endFrame - startFrame))) + startFrame;
			//draw vertical line
			const highlightCanvas = document.createElement('canvas');
			highlightCanvas.width = domCanvas.width;
			highlightCanvas.height = domCanvas.height;
			const ctx = highlightCanvas.getContext('2d') as CanvasRenderingContext2D;
			ctx.strokeStyle = 'white';
			ctx.lineWidth = 1;
			ctx.beginPath();
			const height = dataViewer.clientHeight * 0.98; //1% free space top and bottom
			const width = dataViewer.clientWidth;
			const frameWidth = width / (endFrame - startFrame);
			const frameX = (closestFrame - startFrame) * frameWidth;
			ctx.moveTo(frameX, 0);
			ctx.lineTo(frameX, highlightCanvas.height);
			ctx.stroke();
			//iterate over all graphs and draw the points
			const numGraphs = graphs.length;
			const heightPerGraph =
				(height - dataViewer.clientHeight * 0.02 * (numGraphs - 1)) / numGraphs;
			let heightOffset = 0.01 * dataViewer.clientHeight;
			for (let i = 0; i < numGraphs; i++) {
				const graph = graphs[i];
				const numTraces = graph.length;
				for (let j = 0; j < numTraces; j++) {
					const trace = graph[j];
					const range = trace.maxValue - trace.minValue;
					const scale = heightPerGraph / range;
					ctx.strokeStyle = trace.color;
					ctx.lineWidth = trace.strokeWidth * 2;
					let bbFlag = BB_ALL_FLAGS[trace.flagName];
					if (trace.flagName.startsWith('GEN_'))
						bbFlag = BB_ALL_FLAGS[BB_GEN_FLAGS[trace.flagName].replaces];
					let path = bbFlag?.path || '';
					if (bbFlag?.usesModifier) {
						if (trace.modifier) path += '.' + trace.modifier.toLowerCase();
						else continue;
					}
					const pointY =
						heightOffset +
						heightPerGraph -
						(getNestedProperty(
							sliceAndSkip[Math.floor((closestFrame - startFrame) / skipValue)],
							path,
							{
								max: Math.max(trace.maxValue, trace.minValue),
								min: Math.min(trace.minValue, trace.maxValue)
							}
						) -
							trace.minValue) *
							scale;
					ctx.beginPath();
					ctx.arc(
						(closestFrame - startFrame) * frameWidth,
						pointY,
						trace.strokeWidth * 4,
						0,
						Math.PI * 2
					);
					ctx.stroke();
				}
				heightOffset += heightPerGraph + 0.02 * dataViewer.clientHeight;
			}
			//write down frame number, time in s after start and values next to the cursor at the top
			const frame = sliceAndSkip[closestFrame - startFrame];
			const timeText = (closestFrame / loadedLog!.framesPerSecond).toFixed(3) + 's';
			const valueTexts = [] as string[];
			for (let i = 0; i < numGraphs; i++) {
				const graph = graphs[i];
				const numTraces = graph.length;
				for (let j = 0; j < numTraces; j++) {
					const trace = graph[j];
					if (!trace.flagName) continue;
					let bbFlag = BB_ALL_FLAGS[trace.flagName];
					if (trace.flagName.startsWith('GEN_'))
						bbFlag = BB_ALL_FLAGS[BB_GEN_FLAGS[trace.flagName].replaces];
					let path = bbFlag?.path || '';
					if (bbFlag?.usesModifier) {
						if (trace.modifier) path += '.' + trace.modifier.toLowerCase();
						else continue;
					}
					let value = getNestedProperty(frame, path);
					value = roundToDecimal(value, trace.decimals || bbFlag.decimals || 0);
					if (bbFlag.states) value = bbFlag.states[value] || value;
					if (trace.states) value = trace.states[value] || value;
					valueTexts.push(
						(trace.displayName || bbFlag.name) + ': ' + value + ' ' + (trace.unit || bbFlag.unit)
					);
				}
			}
			const textHeight = 14;
			const textPadding = 6;
			const textBorderRadius = 8;
			ctx.font = '14px sans-serif';
			const textWidth = Math.max(
				...valueTexts.map((t) => ctx.measureText(t).width),
				ctx.measureText(timeText).width
			);
			const textHeightTotal = textHeight * (valueTexts.length + 1) + textPadding * 2;
			const textX = Math.min(
				frameX + 8,
				domCanvas.width - textWidth - textPadding * 2 - textBorderRadius * 2 - 10
			);
			const textY = 8;
			ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
			ctx.beginPath();
			ctx.moveTo(textX + textBorderRadius, textY);
			ctx.lineTo(textX + textWidth + textPadding * 2 + textBorderRadius + 10, textY);
			ctx.arc(
				textX + textWidth + textPadding * 2 + textBorderRadius + 10,
				textY + textBorderRadius,
				textBorderRadius,
				Math.PI * 1.5,
				Math.PI * 2
			);
			ctx.lineTo(
				textX + textWidth + textPadding * 2 + textBorderRadius * 2 + 10,
				textY + textHeightTotal - textBorderRadius
			);
			ctx.arc(
				textX + textWidth + textPadding * 2 + textBorderRadius + 10,
				textY + textHeightTotal - textBorderRadius,
				textBorderRadius,
				0,
				Math.PI * 0.5
			);
			ctx.lineTo(textX + textBorderRadius, textY + textHeightTotal);
			ctx.arc(
				textX + textBorderRadius,
				textY + textHeightTotal - textBorderRadius,
				textBorderRadius,
				Math.PI * 0.5,
				Math.PI
			);
			ctx.lineTo(textX, textY + textBorderRadius);
			ctx.arc(
				textX + textBorderRadius,
				textY + textBorderRadius,
				textBorderRadius,
				Math.PI,
				Math.PI * 1.5
			);
			ctx.fill();
			ctx.fillStyle = 'black';
			ctx.textBaseline = 'top';
			ctx.fillText(timeText, textX + textPadding, textY + textPadding);
			for (let i = 0; i < valueTexts.length; i++) {
				ctx.fillStyle = 'black';
				ctx.fillText(
					valueTexts[i],
					textX + textPadding + 20,
					textY + textPadding + textHeight * (i + 1)
				);
			}
			let pointY = textY + textPadding + textHeight + 6;
			for (let i = 0; i < graphs.length; i++) {
				for (let j = 0; j < graphs[i].length; j++) {
					ctx.fillStyle = graphs[i][j].color;
					ctx.beginPath();
					ctx.arc(textX + textPadding + 8, pointY, 5, 0, Math.PI * 2);
					ctx.fill();
					pointY += textHeight;
					// text
				}
			}
			domCtx.drawImage(highlightCanvas, 0, 0);
			return;
		}
		if (trackingStartX === -1) return;
		if (trackingStartX === -2) {
			const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
			const ratio = (startStartFrame - startEndFrame) / domCanvas.width;
			const diff = e.offsetX - firstX;
			let deltaFrames = Math.floor(diff * ratio);
			if (startEndFrame + deltaFrames > loadedLog!.frameCount - 1)
				deltaFrames = loadedLog!.frameCount - 1 - startEndFrame;
			if (startStartFrame + deltaFrames < 0) deltaFrames = -startStartFrame;
			startFrame = startStartFrame + deltaFrames;
			endFrame = startEndFrame + deltaFrames;
			return;
		}
		trackingEndX = e.clientX;
		const ctx = selectionCanvas.getContext('2d') as CanvasRenderingContext2D;
		ctx.clearRect(0, 0, selectionCanvas.width, selectionCanvas.height);
		ctx.fillStyle = 'rgba(0,0,0,0.2)';
		ctx.fillRect(0, 0, selectionCanvas.width, selectionCanvas.height);
		ctx.clearRect(trackingStartX, 0, trackingEndX - trackingStartX, selectionCanvas.height);
		ctx.strokeStyle = 'white';
		ctx.lineWidth = 1;
		ctx.beginPath();
		ctx.moveTo(trackingStartX, 0);
		ctx.lineTo(trackingStartX, selectionCanvas.height);
		ctx.stroke();
		ctx.beginPath();
		ctx.moveTo(trackingEndX, 0);
		ctx.lineTo(trackingEndX, selectionCanvas.height);
		ctx.stroke();
		const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		const domCtx = domCanvas.getContext('2d') as CanvasRenderingContext2D;
		domCtx.clearRect(0, 0, domCanvas.width, domCanvas.height);
		domCtx.drawImage(canvas, 0, 0);
		domCtx.drawImage(selectionCanvas, 0, 0);
	}
	function onMouseDown(e: MouseEvent) {
		if (!loadedLog || e.button !== 0) return;
		if (!e.shiftKey && (startFrame !== 0 || endFrame !== loadedLog.frameCount - 1)) {
			trackingStartX = -2;
			firstX = e.offsetX;
			startStartFrame = startFrame;
			startEndFrame = endFrame;
			return;
		}
		trackingStartX = e.offsetX;
		trackingEndX = e.offsetX;
		const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		selectionCanvas.width = domCanvas.width;
		selectionCanvas.height = domCanvas.height;
		const ctx = selectionCanvas.getContext('2d') as CanvasRenderingContext2D;
		ctx.clearRect(0, 0, selectionCanvas.width, selectionCanvas.height);
		ctx.fillStyle = 'rgba(0,0,0,0.2)';
		ctx.fillRect(0, 0, selectionCanvas.width, selectionCanvas.height);
		ctx.strokeStyle = 'white';
		ctx.lineWidth = 1;
		ctx.beginPath();
		ctx.moveTo(trackingStartX, 0);
		ctx.lineTo(trackingStartX, selectionCanvas.height);
		ctx.stroke();
		const domCtx = domCanvas.getContext('2d') as CanvasRenderingContext2D;
		domCtx.clearRect(0, 0, domCanvas.width, domCanvas.height);
		domCtx.drawImage(canvas, 0, 0);
		domCtx.drawImage(selectionCanvas, 0, 0);
	}
	function onMouseUp() {
		if (trackingStartX === -1) return;
		if (trackingStartX === -2) {
			trackingStartX = -1;
			return;
		}
		if (Math.abs(trackingStartX - trackingEndX) < 2) {
			trackingStartX = -1;
			drawCanvas(false);
			return;
		}
		if (trackingStartX > trackingEndX) {
			const p = trackingStartX;
			trackingStartX = trackingEndX;
			trackingEndX = p;
		}
		const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		const pStart = startFrame;
		startFrame =
			startFrame + Math.floor((endFrame - startFrame) * (trackingStartX / domCanvas.width));
		endFrame = pStart + Math.floor((endFrame - pStart) * (trackingEndX / domCanvas.width));
		trackingStartX = -1;
	}
	function onMouseWheel(e: WheelEvent) {
		const framesBefore = startFrame,
			framesAfter = loadedLog!.frameCount - 1 - endFrame;
		const visibleFrames = endFrame - startFrame;
		let moveBy = e.deltaY * 0.002 * visibleFrames;
		moveBy = Math.round(moveBy);
		if (-moveBy > framesBefore) moveBy = -framesBefore;
		if (moveBy > framesAfter) moveBy = framesAfter;
		if (moveBy > 0) {
			if (moveBy < 1) moveBy = 1;
			else if (moveBy > visibleFrames * 0.3) moveBy = visibleFrames * 0.3;
		} else {
			if (moveBy > -1) moveBy = -1;
			else if (moveBy < -visibleFrames * 0.3) moveBy = -visibleFrames * 0.3;
		}
		startFrame += moveBy;
		endFrame += moveBy;
	}
	function onMouseLeave(e: MouseEvent) {
		if (e.buttons !== 1) {
			const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
			const domCtx = domCanvas.getContext('2d') as CanvasRenderingContext2D;
			domCtx.clearRect(0, 0, domCanvas.width, domCanvas.height);
			domCtx.drawImage(canvas, 0, 0);
		}
	}

	function deleteLog() {
		const logNum = logNums[selected].num as number;
		port.sendCommand(ConfigCmd.BB_FILE_DELETE, [logNum]);
	}
	function openLog() {
		getLog(selected)
			.then((data) => {
				startFrame = 0;
				endFrame = data.frameCount - 1;
				drawCanvas();
			})
			.catch(console.error);
	}
	function prefixZeros(num: number = 0, totalDigits: number, char: string = '0') {
		let str = num.toString();
		while (str.length < totalDigits) str = char + str;
		return str;
	}
	function downloadLog(type: 'kbb' | 'json' = 'kbb') {
		getLog(selected)
			.then((data) => {
				const file = data.rawFile;
				const blob = new Blob([new Uint8Array(file)], { type: 'application/octet-stream' });
				const blobJSON = new Blob([JSON.stringify(data)], { type: 'application/json' });
				const url = URL.createObjectURL(type === 'json' ? blobJSON : blob);
				//download file
				const a = document.createElement('a');
				a.href = url;
				a.download = `bb${prefixZeros(selected, 2)} ${data.startTime
					.toISOString()
					.replace('T', ' ')
					.replace('.000Z', '')
					.replaceAll('_', '-')}.${type}`;
				a.click();
				URL.revokeObjectURL(url);
			})
			.catch(console.error);
	}
	function openLogFromKbb() {
		const file = prompt('Paste the KBB file or JSON Array here');
		if (!file) return;
		if (file.startsWith('[') && file.endsWith(']')) {
			//JSON array
			try {
				binFile = JSON.parse(file);
			} catch (e) {
				alert('Error parsing JSON: ' + e);
			}
		} else {
			//KBB file
			binFile = file.split('').map((c) => c.charCodeAt(0));
		}
		decodeBinFile();
		startFrame = 0;
		endFrame = (loadedLog?.frameCount || 1) - 1;
		drawCanvas();
	}
	function getLog(num: number): Promise<BBLog> {
		binFileNumber = -1;

		port.sendCommand(ConfigCmd.BB_FILE_DOWNLOAD, [num]);
		return new Promise((resolve, reject) => {
			resolveWhenReady = resolve;
			rejectWrongFile = reject;
		});
	}
	function openLogFromPromptJSON() {
		//use a js alert prompt to get the json file
		const json = prompt('Paste the JSON file here');
		if (!json) return;
		try {
			const data = JSON.parse(json);
			if (data.version[0] !== 0) {
				alert('Wrong version number: ' + data.version);
				return;
			}
			loadedLog = data as BBLog;
			startFrame = 0;
			endFrame = data.frameCount - 1;
			drawCanvas();
		} catch (e) {
			alert('Error parsing JSON: ' + e);
		}
	}
	function formatBB() {
		port.sendCommand(ConfigCmd.BB_FORMAT);
	}

	function addTrace(graphIndex: number) {
		const defaultTrace = {
			color: 'transparent',
			maxValue: 10,
			minValue: 0,
			strokeWidth: 1,
			flagName: '',
			modifier: '',
			id: Math.random()
		};
		graphs[graphIndex] = [...graphs[graphIndex], defaultTrace];
		drawCanvas();
	}
	function addGraph() {
		graphs = [...graphs, []];
		drawCanvas();
	}
	function updateTrace(event: any, graphIndex: number, traceIndex: number, id: number) {
		const tr: TraceInGraph = event.detail;
		graphs[graphIndex][traceIndex] = {
			color: tr.color,
			maxValue: tr.maxValue,
			minValue: tr.minValue,
			strokeWidth: 1,
			flagName: tr.flagName,
			modifier: tr.modifier,
			id,
			unit: tr.unit,
			decimals: tr.decimals,
			states: tr.states,
			displayName: tr.displayName,
			overrideAuto: graphs[graphIndex][traceIndex].overrideAuto
		};
		drawCanvas();
	}
	function getAutoRangeByFlagName(flagName: string) {
		const flag = BB_ALL_FLAGS[flagName];
		const range = { max: 10, min: 0 };
		if (!flag) return range;
		if (flag.rangeFn) return flag.rangeFn(loadedLog);
		range.max = flag.maxValue || 10;
		range.min = flag.minValue || 0;
		return range;
	}
	function deleteGraph(g: number) {
		graphs = graphs.filter((_, i) => i !== g);
		graphs = [...graphs];
		drawCanvas();
	}
	function deleteTrace(g: number, t: number) {
		graphs[g] = graphs[g].filter((_, i) => i !== t);
		graphs = [...graphs];
		drawCanvas();
	}

	function onResize() {
		const canvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		canvas.height = dataViewer.clientHeight;
		canvas.width = dataViewer.clientWidth;

		drawCanvas();
	}
	function getFileList() {
		port.sendCommand(ConfigCmd.BB_FILE_LIST).catch(console.error);
	}
	onMount(() => {
		mounted = true;
		getFileList();
		port.addOnConnectHandler(getFileList);
		dataViewer = document.getElementsByClassName('dataViewerWrapper')[0] as HTMLDivElement;
		window.addEventListener('resize', onResize);
		onResize();
	});
	onDestroy(() => {
		clearTimeout(drawFullCanvasTimeout);
		port.removeOnConnectHandler(getFileList);
		window.removeEventListener('resize', onResize);
	});
</script>

<div class="blackbox">
	<div class="selector">
		<select bind:value={selected}>
			{#each logNums as log}
				<option value={log?.num}>{log?.text || log?.num}</option>
			{/each}
		</select>
		<button on:click={() => openLog()} disabled={selected === -1}>Open</button>
		<button on:click={() => downloadLog()} disabled={selected === -1}>Download KBB</button>
		<button on:click={() => downloadLog('json')} disabled={selected === -1}>Download JSON</button>
		<button on:click={() => deleteLog()} disabled={selected === -1}>Delete</button>
		<button on:click={() => formatBB()}>Format</button>
		<button on:click={() => openLogFromPromptJSON()}>Open JSON</button>
		<button on:click={() => openLogFromKbb()}>Open KBB</button>
		<button on:click={() => (showSettings = true)}>Settings</button>
	</div>
	{#if showSettings}
		<Settings flags={BB_ALL_FLAGS} on:close={() => (showSettings = false)} />
	{/if}
	<div class="dataViewerWrapper">
		<canvas
			id="bbDataViewer"
			on:mousedown={onMouseDown}
			on:mouseup={onMouseUp}
			on:mousemove={onMouseMove}
			on:mouseleave={onMouseLeave}
			on:wheel={onMouseWheel}
			on:dblclick={() => {
				startFrame = 0;
				endFrame = (loadedLog?.frameCount || 1) - 1;
				drawCanvas();
			}}
		/>
	</div>
	<div class="flagSelector">
		{#each graphs as graph, graphIndex}
			<div class="graphSelector">
				{#each graph as trace, traceIndex (trace.id)}
					<TracePlacer
						flags={loadedLog?.flags || []}
						autoRange={trace.overrideAuto ||
							getAutoRangeByFlagName(
								trace.flagName.startsWith('GEN_')
									? BB_GEN_FLAGS[trace.flagName].replaces
									: trace.flagName
							)}
						flagProps={BB_ALL_FLAGS}
						genFlagProps={BB_GEN_FLAGS}
						on:update={(event) => {
							updateTrace(event, graphIndex, traceIndex, trace.id);
						}}
						on:delete={() => {
							deleteTrace(graphIndex, traceIndex);
						}}
						on:overrideAuto={(event) => {
							trace.overrideAuto = event.detail;
						}}
					/>
				{/each}
				<button
					class="addTraceButton"
					disabled={!loadedLog?.flags?.length}
					on:click={() => {
						addTrace(graphIndex);
					}}>Add Trace</button
				>
				<button
					class="deleteGraph"
					disabled={!loadedLog?.flags?.length || graphs.length == 1}
					on:click={() => {
						deleteGraph(graphIndex);
					}}>Del Graph</button
				>
			</div>
		{/each}
		<button class="addGraphButton" disabled={!loadedLog} on:click={addGraph}>Add Graph</button>
		{#if loadedLog}
			<div class="fileInfo" style="margin-top: .8rem">
				<div>Blackbox Version: {loadedLog.version.join('.')}</div>
				<div>Start Time: {loadedLog.startTime.toLocaleString()}</div>
				<div>Frame Count: {loadedLog.frameCount}</div>
				<div>PID Frequency: {loadedLog.pidFrequency} Hz</div>
				<div>Frames per Second: {loadedLog.framesPerSecond} Hz</div>
				<div style="white-space: preserve">
					Flags: {'\n  - ' +
						loadedLog.flags
							.filter((n) => n.startsWith('LOG_'))
							.map((el) => BB_ALL_FLAGS[el].name)
							.join('\n  - ')}
				</div>
				<div>File Size: {(loadedLog.rawFile.length / 1000).toFixed(1)} KB</div>
				<div>
					IMU Range: {loadedLog.ranges.gyro}°/sec, ±{loadedLog.ranges.accel}g
				</div>
				<div>
					PID Gains:
					<div>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ROLL PITCH&nbsp;&nbsp;&nbsp;YAW</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;P:&nbsp;&nbsp;{prefixZeros(loadedLog.pidConstantsNice[0][0], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[1][0], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[2][0], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;I:&nbsp;&nbsp;{prefixZeros(loadedLog.pidConstantsNice[0][1], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[1][1], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[2][1], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;D:&nbsp;&nbsp;{prefixZeros(loadedLog.pidConstantsNice[0][2], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[1][2], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[2][2], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;FF:&nbsp;{prefixZeros(loadedLog.pidConstantsNice[0][3], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[1][3], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[2][3], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;S:&nbsp;&nbsp;{prefixZeros(loadedLog.pidConstantsNice[0][4], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[1][4], 5, ' ')}
						{prefixZeros(loadedLog.pidConstantsNice[2][4], 5, ' ')}
					</div>
				</div>
				<div class="rateFactors">
					Rate Factors:
					<div>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ROLL PITCH&nbsp;&nbsp;&nbsp;YAW</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;x^1:&nbsp;{prefixZeros(loadedLog.rateFactors[0][0], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[0][1], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[0][2], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;x^2:&nbsp;{prefixZeros(loadedLog.rateFactors[1][0], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[1][1], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[1][2], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;x^3:&nbsp;{prefixZeros(loadedLog.rateFactors[2][0], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[2][1], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[2][2], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;x^4:&nbsp;{prefixZeros(loadedLog.rateFactors[3][0], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[3][1], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[3][2], 5, ' ')}
					</div>
					<div style="white-space:pre-wrap">
						&nbsp;&nbsp;x^5:&nbsp;{prefixZeros(loadedLog.rateFactors[4][0], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[4][1], 5, ' ')}
						{prefixZeros(loadedLog.rateFactors[4][2], 5, ' ')}
					</div>
				</div>
			</div>
		{/if}
	</div>
	<div class="timelineWrapper">
		<Timeline
			{loadedLog}
			flagProps={BB_ALL_FLAGS}
			genFlagProps={BB_GEN_FLAGS}
			{startFrame}
			{endFrame}
			on:update={(event) => {
				startFrame = event.detail.startFrame;
				endFrame = event.detail.endFrame;
			}}
		/>
	</div>
</div>

<style>
	.blackbox {
		width: 100%;
		height: 100%;
		box-sizing: border-box;
		display: grid;
		grid-template-rows: 0fr 1fr 0fr;
		grid-template-columns: 1fr 0fr;
		padding-bottom: 0.7rem;
	}

	.selector {
		padding: 0.5rem;
		grid-column: span 2;
	}

	.selector {
		display: flex;
		flex-direction: row;
		align-items: center;
	}

	.selector select {
		width: 14rem;
		background-color: transparent;
		border: 1px solid var(--border-color);
		border-radius: 4px;
		padding: 3px 6px;
		color: var(--text-color);
		outline: none;
	}
	.selector select option {
		color: #444;
	}

	button {
		background-color: transparent;
		border: 1px solid var(--border-color);
		border-radius: 4px;
		padding: 4px 8px;
		color: var(--text-color);
		outline: none;
		margin-left: 0.8rem;
	}
	button[disabled] {
		opacity: 0.5;
		cursor: not-allowed;
	}
	canvas {
		height: 100%;
		width: 100%;
		display: block;
	}
	.dataViewerWrapper {
		position: relative;
		overflow: hidden;
	}
	.timelineWrapper {
		grid-column: span 2;
	}
	.flagSelector {
		width: 21rem;
		overflow: auto;
	}
	.graphSelector {
		padding: 0.8rem;
		border-bottom: var(--border-color) 1px solid;
	}
	.addGraphButton {
		margin-top: 0.8rem;
	}
	.deleteGraph {
		float: right;
	}

	.fileInfo {
		margin-left: 0.5rem;
	}
	.fileInfo div {
		font-family: monospace;
		font-size: medium;
	}
</style>
