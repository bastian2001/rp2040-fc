<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { port, MspFn, MspVersion } from '../../portStore';
	import { configuratorLog } from '../../logStore';
	import TracePlacer from './tracePlacer.svelte';
	import Timeline from './timeline.svelte';
	import Settings from './settings.svelte';
	import {
		leBytesToInt,
		type BBLog,
		type LogFrame,
		type TraceInGraph,
		getNestedProperty,
		roundToDecimal,
		constrain,
		type FlagProps,
		type GenFlagProps,
		prefixZeros,
		map
	} from '../../utils';

	let graphs: TraceInGraph[][] = [[]];

	const ACC_RANGES = [2, 4, 8, 16];
	const GYRO_RANGES = [2000, 1000, 500, 250, 125];

	let dataViewer: HTMLDivElement;

	let dataSlice: LogFrame[] = [];

	let drawFullCanvasTimeout = -1;

	$: dataSlice = loadedLog?.frames.slice(startFrame, endFrame + 1) || [];
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
		file.frames.forEach(f => {
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
		file.frames.forEach(f => {
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
			modifier: [
				{
					displayNameShort: 'RR',
					displayName: 'Rear Right',
					path: 'rr'
				},
				{
					displayNameShort: 'FR',
					displayName: 'Front Right',
					path: 'fr'
				},
				{
					displayNameShort: 'FL',
					displayName: 'Front Left',
					path: 'fl'
				},
				{
					displayNameShort: 'RL',
					displayName: 'Rear Left',
					path: 'rl'
				}
			]
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
			modifier: [
				{
					displayNameShort: 'Year',
					displayName: 'Year',
					min: 2020,
					max: 2030,
					path: 'year'
				},
				{
					displayNameShort: 'Month',
					displayName: 'Month',
					min: 1,
					max: 12,
					path: 'month',
					states: [
						'',
						'January',
						'February',
						'March',
						'April',
						'May',
						'June',
						'July',
						'August',
						'September',
						'October',
						'November',
						'December'
					]
				},
				{
					displayNameShort: 'Day',
					displayName: 'Day',
					min: 1,
					max: 31,
					path: 'day'
				},
				{
					displayNameShort: 'Hour',
					displayName: 'Hour',
					min: 0,
					max: 23,
					path: 'hour'
				},
				{
					displayNameShort: 'Minute',
					displayName: 'Minute',
					min: 0,
					max: 59,
					path: 'minute'
				},
				{
					displayNameShort: 'Second',
					displayName: 'Second',
					min: 0,
					max: 59,
					path: 'second'
				},
				{
					displayNameShort: 'Valid',
					displayName: 'Validity Flags',
					min: 0,
					max: 255,
					path: 'time_validity_flags'
				},
				{
					displayNameShort: 'T Acc',
					displayName: 'Time Accuracy',
					min: 0,
					max: 100,
					path: 't_acc',
					unit: 'ns'
				},
				{
					displayNameShort: 'Nanosec',
					displayName: 'Nanoseconds',
					min: 0,
					max: 1e9,
					path: 'ns',
					unit: 'ns'
				},
				{
					displayNameShort: 'Fix',
					displayName: 'Fix Type',
					min: 0,
					max: 5,
					path: 'fix_type',
					states: [
						'No Fix',
						'Dead reckoning only',
						'2D Fix',
						'3D Fix',
						'GPS + Dead reckoning',
						'Time only fix'
					]
				},
				{
					displayNameShort: 'Flags',
					displayName: 'Flags',
					min: 0,
					max: 255,
					path: 'flags'
				},
				{
					displayNameShort: 'Flags2',
					displayName: 'Flags2',
					min: 0,
					max: 255,
					path: 'flags2'
				},
				{
					displayNameShort: 'Sats',
					displayName: 'Satellite Count',
					min: 0,
					max: 30,
					path: 'sat_count'
				},
				{
					displayNameShort: 'Lon',
					displayName: 'Longitude',
					min: -180,
					max: 180,
					path: 'lon',
					unit: '°',
					decimals: 7
				},
				{
					displayNameShort: 'Lat',
					displayName: 'Latitude',
					min: -90,
					max: 90,
					path: 'lat',
					unit: '°',
					decimals: 7
				},
				{
					displayNameShort: 'Alt',
					displayName: 'Altitude',
					rangeFn: getAltitudeRange,
					path: 'alt',
					unit: 'm',
					decimals: 2
				},
				{
					displayNameShort: 'Hor Acc',
					displayName: 'Horizontal Accuracy',
					min: 0,
					max: 20,
					path: 'h_acc',
					unit: 'm',
					decimals: 2
				},
				{
					displayNameShort: 'Ver Acc',
					displayName: 'Vertical Accuracy',
					min: 0,
					max: 20,
					path: 'v_acc',
					unit: 'm',
					decimals: 2
				},
				{
					displayNameShort: 'Vel N',
					displayName: 'Velocity North',
					min: -10,
					max: 10,
					path: 'vel_n',
					unit: 'm/s',
					decimals: 2
				},
				{
					displayNameShort: 'Vel E',
					displayName: 'Velocity East',
					min: -10,
					max: 10,
					path: 'vel_e',
					unit: 'm/s',
					decimals: 2
				},
				{
					displayNameShort: 'Vel D',
					displayName: 'Velocity Down',
					min: 10,
					max: -10, //down is positive, invert by default
					path: 'vel_d',
					unit: 'm/s',
					decimals: 2
				},
				{
					displayNameShort: 'G Speed',
					displayName: 'Ground Speed',
					min: 0,
					max: 50,
					path: 'g_speed',
					unit: 'm/s',
					decimals: 2
				},
				{
					displayNameShort: 'Head Mot',
					displayName: 'Heading of Motion',
					min: -180,
					max: 180,
					path: 'head_mot',
					unit: '°'
				},
				{
					displayNameShort: 'S Acc',
					displayName: 'Speed Accuracy',
					min: 0,
					max: 10,
					path: 's_acc',
					unit: 'm/s',
					decimals: 2
				},
				{
					displayNameShort: 'Head Acc',
					displayName: 'Heading Accuracy',
					min: 0,
					max: 20,
					path: 'head_acc',
					unit: '°',
					decimals: 2
				},
				{
					displayNameShort: 'pDop',
					displayName: 'pDop',
					min: 0,
					max: 100,
					path: 'p_dop',
					decimals: 2
				},
				{
					displayNameShort: 'Flags3',
					displayName: 'Flags3',
					min: 0,
					max: 31,
					path: 'flags3'
				}
			]
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
			modifier: [
				{
					displayNameShort: 'RR',
					displayName: 'Rear Right',
					path: 'rr'
				},
				{
					displayNameShort: 'FR',
					displayName: 'Front Right',
					path: 'fr'
				},
				{
					displayNameShort: 'FL',
					displayName: 'Front Left',
					path: 'fl'
				},
				{
					displayNameShort: 'RL',
					displayName: 'Rear Left',
					path: 'rl'
				}
			]
		},
		LOG_ACCEL_RAW: {
			name: 'Accel Raw',
			path: 'motion.accelRaw',
			minValue: -40,
			maxValue: 40,
			unit: 'm/s²',
			decimals: 3,
			modifier: [
				{
					displayNameShort: 'X',
					displayName: 'X',
					path: 'x'
				},
				{
					displayNameShort: 'Y',
					displayName: 'Y',
					path: 'y'
				},
				{
					displayNameShort: 'Z',
					displayName: 'Z',
					path: 'z'
				}
			]
		},
		LOG_ACCEL_FILTERED: {
			name: 'Accel Filtered',
			path: 'motion.accelFiltered',
			minValue: -40,
			maxValue: 40,
			unit: 'm/s²',
			decimals: 3,
			modifier: [
				{
					displayNameShort: 'X',
					displayName: 'X',
					path: 'x'
				},
				{
					displayNameShort: 'Y',
					displayName: 'Y',
					path: 'y'
				},
				{
					displayNameShort: 'Z',
					displayName: 'Z',
					path: 'z'
				}
			]
		},
		LOG_VERTICAL_ACCEL: {
			name: 'Vertical Accel',
			path: 'motion.accelVertical',
			minValue: -40,
			maxValue: 40,
			unit: 'm/s²',
			decimals: 3
		},
		LOG_VVEL_SETPOINT: {
			name: 'vVel Setpoint',
			path: 'setpoint.vvel',
			minValue: -10,
			maxValue: 10,
			decimals: 2,
			unit: 'm/s'
		},
		LOG_MAG_HEADING: {
			name: 'Mag Heading',
			path: 'motion.magHeading',
			minValue: -180,
			maxValue: 180,
			unit: '°'
		},
		LOG_COMBINED_HEADING: {
			name: 'Combined Heading',
			path: 'motion.combinedHeading',
			minValue: -180,
			maxValue: 180,
			unit: '°'
		},
		LOG_HVEL: {
			name: 'Hor. Velocity',
			path: 'motion.hvel',
			minValue: -10,
			maxValue: 10,
			unit: 'm/s',
			decimals: 2,
			modifier: [
				{
					displayNameShort: 'North',
					displayName: 'North',
					path: 'n'
				},
				{
					displayNameShort: 'East',
					displayName: 'East',
					path: 'e'
				}
			]
		}
	} as {
		[key: string]: FlagProps;
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
		GEN_VVEL_SETPOINT: {
			name: 'vVel Setpoint',
			replaces: 'LOG_VVEL_SETPOINT',
			requires: ['LOG_THROTTLE_ELRS_RAW'],
			unit: 'm/s',
			exact: false
		}
	} as {
		[key: string]: GenFlagProps;
	};

	function fillLogWithGenFlags(log: BBLog) {
		log.isExact = true;
		const genFlags = Object.keys(BB_GEN_FLAGS);
		for (let i = 0; i < genFlags.length; i++) {
			const flagName = genFlags[i];
			const flag = BB_GEN_FLAGS[flagName];
			if (log.flags.includes(flag.replaces)) continue;
			if (
				flag.requires.every(r => {
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
						log.frames.forEach(f => {
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
						log.frames.forEach(f => {
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
						log.frames.forEach(f => {
							f.setpoint.throttle = f.elrs.throttle;
						});
						break;
					case 'GEN_YAW_SETPOINT':
						log.frames.forEach(f => {
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
						log.frames.forEach(f => {
							f.pid.roll.p = (f.setpoint.roll! - f.gyro.roll!) * log.pidConstants[0][0];
						});
						break;
					case 'GEN_PITCH_PID_P':
						log.frames.forEach(f => {
							f.pid.pitch.p = (f.setpoint.pitch! - f.gyro.pitch!) * log.pidConstants[1][0];
						});
						break;
					case 'GEN_YAW_PID_P':
						log.frames.forEach(f => {
							f.pid.yaw.p = (f.setpoint.yaw! - f.gyro.yaw!) * log.pidConstants[2][0];
						});
						break;
					case 'GEN_ROLL_PID_D':
						log.frames.forEach((f, i) => {
							f.pid.roll.d =
								(((log.frames[i - 1]?.gyro.roll || 0) - f.gyro.roll!) * log.pidConstants[0][2]) /
								log.frequencyDivider;
						});
						break;
					case 'GEN_PITCH_PID_D':
						log.frames.forEach((f, i) => {
							f.pid.pitch.d =
								(((log.frames[i - 1]?.gyro.pitch || 0) - f.gyro.pitch!) * log.pidConstants[1][2]) /
								log.frequencyDivider;
						});
						break;
					case 'GEN_YAW_PID_D':
						log.frames.forEach((f, i) => {
							f.pid.yaw.d =
								(((log.frames[i - 1]?.gyro.yaw || 0) - f.gyro.yaw!) * log.pidConstants[2][2]) /
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
						log.frames.forEach(f => {
							f.pid.roll.s = f.setpoint.roll! * log.pidConstants[0][4];
						});
						break;
					case 'GEN_PITCH_PID_S':
						log.frames.forEach(f => {
							f.pid.pitch.s = f.setpoint.pitch! * log.pidConstants[1][4];
						});
						break;
					case 'GEN_YAW_PID_S':
						log.frames.forEach(f => {
							f.pid.yaw.s = f.setpoint.yaw! * log.pidConstants[2][4];
						});
						break;
					case 'GEN_ROLL_PID_I':
						{
							let rollI = 0;
							let takeoffCounter = 0;
							log.frames.forEach(f => {
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
							log.frames.forEach(f => {
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
							log.frames.forEach(f => {
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
						log.frames.forEach(f => {
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
					case 'GEN_VVEL_SETPOINT':
						log.frames.forEach(f => {
							let t = (f.elrs.throttle! - 1500) * 2;
							if (t > 0) {
								t -= 100;
								if (t < 0) t = 0;
							} else if (t < 0) {
								t += 100;
								if (t > 0) t = 0;
							}
							f.setpoint.vvel = t / 180;
							if (f.flightMode !== undefined && f.flightMode < 2) f.setpoint.vvel = 0;
						});
						break;
				}
			}
		}
	}

	const unsubscribe = port.subscribe(command => {
		if (command.cmdType === 'response') {
			switch (command.command) {
				case MspFn.BB_FILE_LIST:
					logNums = command.data.map(n => ({ text: '', num: n }));
					if (!logNums.length) {
						logNums = [{ text: 'No logs found', num: -1 }];
						selected = -1;
						return;
					}
					selected = logNums[0].num;
					logInfoPosition = 0;
					logInfoInterval = setInterval(getLogInfo, 100);
					break;
				case MspFn.BB_FILE_INFO:
					processLogInfo(command.data);
					break;
				case MspFn.BB_FILE_DOWNLOAD:
					handleFileChunk(command.data);
					break;
				case MspFn.BB_FILE_DELETE:
					const index = logNums.findIndex(l => l.num === selected);
					if (index !== -1) logNums.splice(index, 1);
					logNums = [...logNums];
					if (!logNums.length) {
						logNums = [{ text: 'No logs found', num: -1 }];
						selected = -1;
					} else if (index >= logNums.length) selected = logNums[logNums.length - 1].num;
					else selected = logNums[index].num;
					break;
				case MspFn.BB_FORMAT:
					configuratorLog.push('Blackbox formatted');
					break;
			}
		} else if (command.cmdType === 'error') {
			switch (command.command) {
				case MspFn.BB_FORMAT:
					configuratorLog.push('Blackbox format failed');
					break;
				case MspFn.BB_FILE_DELETE:
					configuratorLog.push(`Deleting file ${command.data[0]} failed`);
					break;
			}
		}
	});

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
					port.sendCommand('request', MspFn.BB_FILE_DOWNLOAD, MspVersion.V2, [
						binFileNumber,
						i & 0xff,
						(i >> 8) & 0xff,
						(i >> 16) & 0xff,
						(i >> 24) & 0xff
					]);
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
		const sTime = leBytesToInt(header.slice(7, 11)); // unix timestamp in seconds (UTC)
		const startTime = new Date(sTime * 1000);
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
				if ([35, 36, 37].includes(i + 32)) frameSize += 6;
				else if ([42].includes(i + 32)) frameSize += 4;
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
				motion: { gps: {}, accelRaw: {}, accelFiltered: {}, hvel: {} },
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
					leBytesToInt(data.slice(i + offsets['LOG_VVEL'], i + offsets['LOG_VVEL'] + 2), true) /
					256;
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
					((leBytesToInt(
						data.slice(i + offsets['LOG_ATT_ROLL'], i + offsets['LOG_ATT_ROLL'] + 2),
						true
					) /
						10000) *
						180) /
					Math.PI;
			}
			if (flags.includes('LOG_ATT_PITCH')) {
				// pitch = 0.0001 * raw (signed 16 bit int)
				frame.attitude.pitch =
					((leBytesToInt(
						data.slice(i + offsets['LOG_ATT_PITCH'], i + offsets['LOG_ATT_PITCH'] + 2),
						true
					) /
						10000) *
						180) /
					Math.PI;
			}
			if (flags.includes('LOG_ATT_YAW')) {
				// yaw = 0.0001 * raw (signed 16 bit int)
				frame.attitude.yaw =
					((leBytesToInt(
						data.slice(i + offsets['LOG_ATT_YAW'], i + offsets['LOG_ATT_YAW'] + 2),
						true
					) /
						10000) *
						180) /
					Math.PI;
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
			if (flags.includes('LOG_ACCEL_FILTERED')) {
				const accelBytes = data.slice(
					i + offsets['LOG_ACCEL_FILTERED'],
					i + offsets['LOG_ACCEL_FILTERED'] + 6
				);
				frame.motion.accelFiltered.x = (leBytesToInt(accelBytes.slice(0, 2), true) * 9.81) / 2048;
				frame.motion.accelFiltered.y = (leBytesToInt(accelBytes.slice(2, 4), true) * 9.81) / 2048;
				frame.motion.accelFiltered.z = (leBytesToInt(accelBytes.slice(4, 6), true) * 9.81) / 2048;
			}
			if (flags.includes('LOG_VERTICAL_ACCEL')) {
				frame.motion.accelVertical =
					leBytesToInt(
						data.slice(i + offsets['LOG_VERTICAL_ACCEL'], i + offsets['LOG_VERTICAL_ACCEL'] + 2),
						true
					) / 128;
			}
			if (flags.includes('LOG_VVEL_SETPOINT')) {
				frame.setpoint.vvel =
					leBytesToInt(
						data.slice(i + offsets['LOG_VVEL_SETPOINT'], i + offsets['LOG_VVEL_SETPOINT'] + 2),
						true
					) / 4096;
			}
			if (flags.includes('LOG_MAG_HEADING')) {
				frame.motion.magHeading =
					((leBytesToInt(
						data.slice(i + offsets['LOG_MAG_HEADING'], i + offsets['LOG_MAG_HEADING'] + 2),
						true
					) /
						8192) *
						180) /
					Math.PI;
			}
			if (flags.includes('LOG_COMBINED_HEADING')) {
				frame.motion.combinedHeading =
					((leBytesToInt(
						data.slice(
							i + offsets['LOG_COMBINED_HEADING'],
							i + offsets['LOG_COMBINED_HEADING'] + 2
						),
						true
					) /
						8192) *
						180) /
					Math.PI;
			}
			if (flags.includes('LOG_HVEL')) {
				frame.motion.hvel.n =
					leBytesToInt(data.slice(i + offsets['LOG_HVEL'], i + offsets['LOG_HVEL'] + 2), true) /
					256;
				frame.motion.hvel.e =
					leBytesToInt(data.slice(i + offsets['LOG_HVEL'] + 2, i + offsets['LOG_HVEL'] + 4), true) /
					256;
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
		port.sendCommand('request', MspFn.BB_FILE_INFO, MspVersion.V2, infoNums);
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
			const sTime = leBytesToInt(data.slice(i + 8, i + 12), false); // unix timestamp in seconds (UTC)
			const startTime = new Date(sTime * 1000);
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
				if (j == 26 || [35, 36, 37].includes(j)) frameSize += 6;
				else if (j == 28) frameSize++;
				else frameSize += 2;
			}
			const frames = dataBytes / frameSize;
			//append duration of log file to logNums
			const index = logNums.findIndex(n => n.num == fileNum);
			if (index == -1) continue;
			const duration = Math.round(frames / framesPerSecond);
			logNums[index].text = `${logNums[index].num} - ${duration}s - ${startTime.toLocaleString()}`;
			selected = fileNum;
		}
	}

	const canvas = document.createElement('canvas');
	let sliceAndSkip = [] as LogFrame[];
	let skipValue = 0;
	const durationBarRaster = [
		'1ms',
		'2ms',
		'5ms',
		'10ms',
		'20ms',
		'50ms',
		'100ms',
		'200ms',
		'0.5s',
		'1s',
		'2s',
		'5s',
		'10s',
		'20s',
		'30s',
		'1min',
		'2min',
		'5min'
	];
	function decodeDuration(duration: string): number {
		let seconds = parseFloat(duration.replaceAll(/[a-zA-Z]/g, ''));
		if (duration.endsWith('min')) seconds *= 60;
		else if (duration.endsWith('ms')) seconds *= 0.001;
		return seconds;
	}
	function drawCanvas(allowShortening = true) {
		if (!mounted || !loadedLog) return;
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
		const pixelsPerSec =
			(dataViewer.clientWidth * loadedLog!.framesPerSecond) / (dataSlice.length - 1);
		//filter out all the ones that don't fit
		let durations = durationBarRaster.filter(el => {
			const seconds = decodeDuration(el);
			if (seconds * pixelsPerSec >= dataViewer.clientWidth - 80) return false;
			if (seconds * pixelsPerSec <= (dataViewer.clientWidth - 80) * 0.1) return false;
			return true;
		});
		let barDuration = '';
		for (let i = 0; i < durations.length - 1; i++) {
			if (pixelsPerSec * decodeDuration(durations[i]) > 200) {
				barDuration = durations[i];
				break;
			}
		}
		barDuration = barDuration || durations[durations.length - 1];
		const barLength = pixelsPerSec * decodeDuration(barDuration || '1s');
		ctx.clearRect(0, 0, dataViewer.clientWidth, dataViewer.clientHeight);
		ctx.strokeStyle = 'rgba(255, 255, 255, 1)';
		ctx.lineWidth = 3;
		ctx.textAlign = 'center';
		ctx.font = '16px sans-serif';
		ctx.textBaseline = 'bottom';
		ctx.fillStyle = 'white';
		if (barDuration) {
			ctx.beginPath();
			ctx.moveTo(16, 40);
			ctx.lineTo(16 + barLength, 40);
			ctx.stroke();
			ctx.fillText(barDuration, 16 + barLength / 2, 35);
		}

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
				trace.overrideSliceAndSkip = [];
				if (trace.overrideData) {
					const overrideSlice = trace.overrideData.slice(
						Math.max(0, Math.min(startFrame, endFrame)),
						Math.max(0, Math.max(startFrame, endFrame)) + 1
					);
					if (everyNth > 2 && allowShortening) {
						const len = overrideSlice.length;
						for (let i = 0; i < len; i += everyNth) {
							trace.overrideSliceAndSkip.push(overrideSlice[i]);
						}
					} else trace.overrideSliceAndSkip = overrideSlice;
				}
				let bbFlag = BB_ALL_FLAGS[trace.flagName];
				if (trace.flagName.startsWith('GEN_'))
					bbFlag = BB_ALL_FLAGS[BB_GEN_FLAGS[trace.flagName].replaces];
				let path = bbFlag?.path || '';
				if (bbFlag?.modifier) {
					if (trace.modifier) path += '.' + trace.modifier.toLowerCase();
					else continue;
				}
				ctx.beginPath();
				let pointY =
					heightOffset +
					heightPerGraph -
					((trace.overrideData
						? constrain(trace.overrideSliceAndSkip[0], trace.minValue, trace.maxValue)
						: getNestedProperty(sliceAndSkip[0], path, {
								max: Math.max(trace.maxValue, trace.minValue),
								min: Math.min(trace.minValue, trace.maxValue)
							})) -
						trace.minValue) *
						scale;
				ctx.moveTo(0, pointY);
				for (let k = 1; k < sliceAndSkip.length; k++) {
					pointY =
						heightOffset +
						heightPerGraph -
						((trace.overrideData
							? constrain(trace.overrideSliceAndSkip[k], trace.minValue, trace.maxValue)
							: getNestedProperty(sliceAndSkip[k], path, {
									max: Math.max(trace.maxValue, trace.minValue),
									min: Math.min(trace.minValue, trace.maxValue)
								})) -
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
	function drawSelection(startX: number, endX: number) {
		const ctx = selectionCanvas.getContext('2d') as CanvasRenderingContext2D;
		ctx.clearRect(0, 0, selectionCanvas.width, selectionCanvas.height);
		ctx.fillStyle = 'rgba(0,0,0,0.2)';
		ctx.fillRect(0, 0, selectionCanvas.width, selectionCanvas.height);
		ctx.clearRect(startX, 0, endX - startX, selectionCanvas.height);
		ctx.strokeStyle = 'white';
		ctx.lineWidth = 1;
		ctx.beginPath();
		ctx.moveTo(startX, 0);
		ctx.lineTo(startX, selectionCanvas.height);
		ctx.stroke();
		ctx.beginPath();
		ctx.moveTo(endX, 0);
		ctx.lineTo(endX, selectionCanvas.height);
		ctx.stroke();
		const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		const domCtx = domCanvas.getContext('2d') as CanvasRenderingContext2D;
		domCtx.clearRect(0, 0, domCanvas.width, domCanvas.height);
		domCtx.drawImage(canvas, 0, 0);
		domCtx.drawImage(selectionCanvas, 0, 0);
	}
	function onMouseMove(e: MouseEvent) {
		if (!loadedLog) return;
		if (e.buttons !== 1) {
			onMouseUp();
			// highlight all points on the current frame
			const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
			const domCtx = domCanvas.getContext('2d') as CanvasRenderingContext2D;
			domCtx.clearRect(0, 0, domCanvas.width, domCanvas.height);
			domCtx.drawImage(canvas, 0, 0);
			const closestFrameSliceSkip = Math.round(
				(e.offsetX / domCanvas.width) * (sliceAndSkip.length - 1)
			);
			const closestFrameNum = startFrame + closestFrameSliceSkip * skipValue;
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
			const frameWidth = width / (sliceAndSkip.length - 1);
			const frameX = closestFrameSliceSkip * frameWidth;
			ctx.moveTo(frameX, 0);
			ctx.lineTo(frameX, highlightCanvas.height);
			ctx.stroke();
			//iterate over all graphs and draw the points
			const numGraphs = graphs.length;
			const heightPerGraph =
				(height - dataViewer.clientHeight * 0.02 * (numGraphs - 1)) / numGraphs;
			let heightOffset = 0.01 * dataViewer.clientHeight;
			const frame = sliceAndSkip[closestFrameSliceSkip];
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
					if (bbFlag?.modifier) {
						if (trace.modifier) path += '.' + trace.modifier.toLowerCase();
						else continue;
					}
					const pointY =
						heightOffset +
						heightPerGraph -
						((trace.overrideData
							? constrain(
									trace.overrideSliceAndSkip![closestFrameSliceSkip],
									trace.minValue,
									trace.maxValue
								)
							: getNestedProperty(frame, path, {
									max: Math.max(trace.maxValue, trace.minValue),
									min: Math.min(trace.minValue, trace.maxValue)
								})) -
							trace.minValue) *
							scale;
					ctx.beginPath();
					ctx.arc(frameX, pointY, trace.strokeWidth * 4, 0, Math.PI * 2);
					ctx.stroke();
				}
				heightOffset += heightPerGraph + 0.02 * dataViewer.clientHeight;
			}
			//write down frame number, time in s after start and values next to the cursor at the top
			const timeText =
				(closestFrameNum / loadedLog!.framesPerSecond).toFixed(3) + 's, Frame ' + closestFrameNum;
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
					if (bbFlag?.modifier) {
						if (trace.modifier) path += '.' + trace.modifier.toLowerCase();
						else continue;
					}
					let value = trace.overrideData
						? trace.overrideSliceAndSkip![closestFrameSliceSkip]
						: getNestedProperty(frame, path);
					value = roundToDecimal(value, trace.decimals);
					if (bbFlag.states) value = bbFlag.states[value] || value;
					if (trace.states) value = trace.states[value] || value;
					valueTexts.push(trace.displayName + ': ' + value + ' ' + trace.unit);
				}
			}
			const textHeight = 14;
			const textPadding = 6;
			const textBorderRadius = 8;
			ctx.font = '14px sans-serif';
			const textWidth = Math.max(
				...valueTexts.map(t => ctx.measureText(t).width),
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
					if (!graphs[i][j].flagName) continue;
					ctx.fillStyle = graphs[i][j].color;
					ctx.beginPath();
					ctx.arc(textX + textPadding + 8, pointY, 5, 0, Math.PI * 2);
					ctx.fill();
					pointY += textHeight;
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
		drawSelection(trackingStartX, trackingEndX);
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
		drawSelection(trackingStartX, trackingEndX);
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
		const nStart =
			startFrame + Math.floor((endFrame - startFrame) * (trackingStartX / domCanvas.width));
		const nEnd =
			startFrame + Math.floor((endFrame - startFrame) * (trackingEndX / domCanvas.width));
		startFrame = Math.min(nStart, nEnd);
		endFrame = Math.max(nStart, nEnd);
		trackingStartX = -1;
	}
	function onMouseWheel(e: WheelEvent) {
		e.preventDefault();
		if (!loadedLog) return;
		if (e.getModifierState('Control')) {
			let visibleFrames = endFrame - startFrame;
			const leftPct = e.offsetX / dataViewer.clientWidth;
			const grabFrame = startFrame + visibleFrames * leftPct;
			const zoomFactor = e.deltaY > 0 ? 1.333 : 0.75;
			visibleFrames *= zoomFactor;
			if (visibleFrames < 10) visibleFrames = 10;
			startFrame = grabFrame - visibleFrames * leftPct;
			endFrame = startFrame + visibleFrames;
			startFrame = Math.round(startFrame);
			endFrame = Math.round(endFrame);
			visibleFrames = endFrame - startFrame;
			if (startFrame < 0) {
				startFrame = 0;
				endFrame = visibleFrames;
			}
			if (endFrame > loadedLog.frameCount - 1) {
				endFrame = loadedLog.frameCount - 1;
				startFrame = endFrame - visibleFrames;
			}
			if (startFrame < 0) {
				startFrame = 0;
				endFrame = loadedLog.frameCount - 1;
			}
			return;
		}
		const visibleFrames = endFrame - startFrame;
		let moveBy = e.deltaY * 0.002 * visibleFrames;
		if (moveBy > 0 && moveBy < 1) moveBy = 1;
		if (moveBy < 0 && moveBy > -1) moveBy = -1;
		if (moveBy > visibleFrames * 0.3) moveBy = visibleFrames * 0.3;
		if (moveBy < -visibleFrames * 0.3) moveBy = -visibleFrames * 0.3;
		moveBy = Math.round(moveBy);
		if (startFrame + moveBy < 0) moveBy = -startFrame;
		if (endFrame + moveBy > loadedLog.frameCount - 1) moveBy = loadedLog.frameCount - 1 - endFrame;
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
	let touchStartX = 0,
		touchEndX = 0,
		frame0 = 0,
		frame1 = 0; //0 and 1 are the touch identifiers, and frames are the frames they are clamped to
	let touchMode: 'none' | 'move' | 'zoom' = 'none';
	function onTouchDown(e: TouchEvent) {
		e.preventDefault();
		if (!loadedLog) return;
		const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		const touches = [];
		const changedTouches = [];
		for (let i = 0; i < e.touches.length; i++)
			if (e.touches[i].identifier < 2) touches.push(e.touches[i]);
		for (let i = 0; i < e.changedTouches.length; i++)
			if (e.changedTouches[i].identifier < 2) changedTouches.push(e.changedTouches[i]);

		if (changedTouches.length === 1) {
			if (touchMode === 'none') {
				touchMode = 'move';
				touchStartX = changedTouches[0].clientX;
				startStartFrame = startFrame;
				startEndFrame = endFrame;
			} else {
				touchMode = 'zoom';
				frame0 =
					startFrame + Math.round((endFrame - startFrame) * (touches[0].clientX / domCanvas.width));
				frame1 =
					startFrame + Math.round((endFrame - startFrame) * (touches[1].clientX / domCanvas.width));
				if (frame1 === frame0) {
					frame1++;
					if (frame1 > loadedLog.frameCount - 1) {
						frame1 = loadedLog.frameCount - 1;
						frame0 = frame1 - 1;
					}
				}
			}
		} else {
			touchMode = 'zoom';
			frame0 =
				startFrame + Math.round((endFrame - startFrame) * (touches[0].clientX / domCanvas.width));
			frame1 =
				startFrame + Math.round((endFrame - startFrame) * (touches[1].clientX / domCanvas.width));
			if (frame1 === frame0) {
				frame1++;
				if (frame1 > loadedLog.frameCount - 1) {
					frame1 = loadedLog.frameCount - 1;
					frame0 = frame1 - 1;
				}
			}
		}
	}
	function onTouchMove(e: TouchEvent) {
		e.preventDefault();
		if (!loadedLog) return;
		const domCanvas = document.getElementById('bbDataViewer') as HTMLCanvasElement;
		const touches = [];
		for (let i = 0; i < e.touches.length; i++)
			if (e.touches[i].identifier < 2) touches.push(e.touches[i]);
		if (touchMode === 'move') {
			const diff = touches[0].clientX - touchStartX;
			const ratio = (startStartFrame - startEndFrame) / domCanvas.width;
			let deltaFrames = Math.floor(diff * ratio);
			if (startEndFrame + deltaFrames > loadedLog!.frameCount - 1)
				deltaFrames = loadedLog!.frameCount - 1 - startEndFrame;
			if (startStartFrame + deltaFrames < 0) deltaFrames = -startStartFrame;
			startFrame = startStartFrame + deltaFrames;
			endFrame = startEndFrame + deltaFrames;
		}
		if (touchMode === 'zoom') {
			let span = Math.round(
				((frame1 - frame0) * domCanvas.width) / (e.touches[1].clientX - e.touches[0].clientX)
			);
			if (span < 0) span = loadedLog.frameCount - 1;
			if (span >= loadedLog.frameCount - 1) {
				startFrame = 0;
				endFrame = loadedLog.frameCount - 1;
				return;
			}
			const frameCenter = Math.round((frame0 + frame1) / 2);
			const centerPos = (e.touches[0].clientX + e.touches[1].clientX) / 2;
			startFrame = frameCenter - Math.round(span * (centerPos / domCanvas.width));
			endFrame = frameCenter + Math.round(span * (1 - centerPos / domCanvas.width));
			if (startFrame < 0) {
				endFrame -= startFrame;
				startFrame = 0;
			}
			if (endFrame >= loadedLog.frameCount) {
				startFrame -= endFrame - loadedLog.frameCount + 1;
				endFrame = loadedLog.frameCount - 1;
			}
		}
	}
	function onTouchUp(e: TouchEvent) {
		e.preventDefault();
		if (!loadedLog) return;
		if (touchMode === 'none') return;
		if (touchMode === 'move' && (e.touches[0]?.identifier || 2) >= 2) {
			touchMode = 'none';
			return;
		}
		if (touchMode === 'zoom') {
			if (
				(e.touches[0]?.identifier === 0 || e.touches[0]?.identifier === 1) &&
				e.touches[1]?.identifier !== 1
			) {
				touchMode = 'move';
				touchStartX = e.touches[0].clientX;
				startStartFrame = startFrame;
				startEndFrame = endFrame;
				return;
			} else if (e.touches[1]?.identifier !== 1) {
				touchMode = 'none';
			}
		}
	}

	function deleteLog() {
		port.sendCommand('request', MspFn.BB_FILE_DELETE, MspVersion.V2, [selected]);
	}
	function openLog() {
		getLog(selected)
			.then(data => {
				startFrame = 0;
				endFrame = data.frameCount - 1;
				drawCanvas();
			})
			.catch(console.error);
	}
	function downloadLog(type: 'kbb' | 'json' = 'kbb') {
		getLog(selected)
			.then(data => {
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
	function openLogFromFile() {
		const input = document.createElement('input');
		input.type = 'file';
		input.accept = '.kbb, .json';
		input.onchange = () => {
			const file = input.files?.[0];
			if (!file) return;
			if (file.name.endsWith('.json')) {
				const reader = new FileReader();
				reader.onload = () => {
					const str = reader.result as string;
					try {
						const data = JSON.parse(str);
						if (data.version[0] !== 0) {
							alert('Wrong version number: ' + data.version);
							return;
						}
						loadedLog = data as BBLog;
						startFrame = 0;
						endFrame = data.frameCount - 1;
						drawCanvas();
					} catch (e) {}
				};
				reader.readAsText(file);
			} else {
				const reader = new FileReader();
				reader.onload = () => {
					const data = reader.result as ArrayBuffer;
					binFile = [];
					const view = new Uint8Array(data);
					for (let i = 0; i < view.length; i++) {
						binFile.push(view[i]);
					}

					decodeBinFile();
					startFrame = 0;
					endFrame = (loadedLog?.frameCount || 1) - 1;
					drawCanvas();
				};
				reader.readAsArrayBuffer(file);
			}
		};
		input.click();
	}
	function getLog(num: number): Promise<BBLog> {
		binFileNumber = -1;

		port.sendCommand('request', MspFn.BB_FILE_DOWNLOAD, MspVersion.V2, [num]);
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
		port.sendCommand('request', MspFn.BB_FORMAT);
	}

	function addTrace(graphIndex: number) {
		const defaultTrace = {
			color: 'transparent',
			maxValue: 10,
			minValue: 0,
			strokeWidth: 1,
			flagName: '',
			modifier: '',
			decimals: 0,
			unit: '',
			displayName: '',
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
		tr.id = id;
		graphs[graphIndex][traceIndex] = tr;
		drawCanvas();
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
		selectionCanvas.height = dataViewer.clientHeight;
		selectionCanvas.width = dataViewer.clientWidth;

		drawCanvas();
	}
	function getFileList() {
		port.sendCommand('request', MspFn.BB_FILE_LIST).catch(console.error);
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
		unsubscribe();
	});
	// use with caution, only exists because svelte does not allow typescript in the template
	function logButItsThere() {
		return loadedLog!;
	}
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
		<button on:click={() => openLogFromFile()}>Open from file</button>
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
			on:touchstart={onTouchDown}
			on:touchmove={onTouchMove}
			on:touchend={onTouchUp}
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
						log={logButItsThere()}
						flagProps={BB_ALL_FLAGS}
						genFlagProps={BB_GEN_FLAGS}
						on:update={event => {
							updateTrace(event, graphIndex, traceIndex, trace.id);
						}}
						on:delete={() => {
							deleteTrace(graphIndex, traceIndex);
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
				<div style="white-space: pre">
					Flags: {'\n  - ' +
						loadedLog.flags
							.filter(n => n.startsWith('LOG_'))
							.map(el => BB_ALL_FLAGS[el].name)
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
			on:update={event => {
				startFrame = Math.min(event.detail.startFrame, event.detail.endFrame);
				endFrame = Math.max(event.detail.startFrame, event.detail.endFrame);
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
		width: 23rem;
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
