<script lang="ts">
	import { port, ConfigCmd } from '../stores';
	import { onMount, onDestroy } from 'svelte';
	import type { Command } from '../stores';
	import { leBytesToInt, roundToDecimal } from '../utils';

	const FLIGHT_MODES = ['ACRO', 'ANGLE', 'ALT_HOLD', 'GPS_VEL', 'GPS_POS'];
	const ARMING_DISABLE_FLAGS = [
		'Arming switch not armed',
		'Throttle up',
		'No GPS fix',
		'Configurator attached',
		'ELRS missing',
		'Not Acro or Angle',
		'Gyro not calibrated'
	];
	let flightMode = 0;
	let armingDisableFlags = 0;
	let armed = false;
	let configuratorConnected = false;
	let pingFromConfigurator = -1,
		pingFromFC = -1;

	let getRotationInterval = 0;
	let xBox: any = null;
	let yBox: any = null;
	let zBox: any = null;
	let attitude = {
		roll: 0,
		pitch: 0,
		yaw: 0
	};
	let serialNum = 1;
	let baudRate = 115200;
	let getGpsData = 0,
		gpsDataSlow = 0;
	let gpsAcc: {
		tAcc: number;
		hAcc: number;
		vAcc: number;
		headAcc: number;
		sAcc: number;
		pDop: number;
	} = {
		tAcc: 1000,
		hAcc: 1000,
		vAcc: 1000,
		headAcc: 1000,
		sAcc: 1000,
		pDop: 1000
	};
	let gpsStatus: {
		gpsInited: boolean;
		initStep: number;
		fix: number;
		timeValidityFlags: number;
		flags: number;
		flags2: number;
		flags3: number;
		satCount: number;
	} = {
		gpsInited: false,
		initStep: 0,
		fix: 0,
		timeValidityFlags: 0,
		flags: 0,
		flags2: 0,
		flags3: 0,
		satCount: 0
	};
	let gpsMotion: {
		lat: number;
		lon: number;
		alt: number;
		velN: number;
		velE: number;
		velD: number;
		gSpeed: number;
		headMot: number;
	} = {
		velN: 0,
		velE: 0,
		velD: 0,
		gSpeed: 0,
		headMot: 0,
		lat: 0,
		lon: 0,
		alt: 0
	};
	let gpsTime: {
		year: number;
		month: number;
		day: number;
		hour: number;
		minute: number;
		second: number;
	} = {
		year: 0,
		month: 0,
		day: 0,
		hour: 0,
		minute: 0,
		second: 0
	};
	let combinedAltitude = 0,
		verticalVelocity = 0;

	$: handleCommand($port);
	function handleCommand(command: Command) {
		switch (command.command) {
			case ConfigCmd.STATUS | 0x4000:
				armed = command.data[2] === 1;
				flightMode = command.data[3];
				armingDisableFlags = leBytesToInt(command.data.slice(4, 8));
				configuratorConnected = command.data[8] === 1;
				break;
			case ConfigCmd.GET_ROTATION | 0x4000:
				let pitch = leBytesToInt(command.data.slice(0, 2), true);
				pitch /= 8192.0;
				pitch *= 180.0 / Math.PI;
				let roll = leBytesToInt(command.data.slice(2, 4), true);
				roll /= 8192.0;
				roll *= 180.0 / Math.PI;
				let yaw = leBytesToInt(command.data.slice(4, 6), true);
				yaw /= 8192.0;
				yaw *= 180.0 / Math.PI;
				zBox.style.transform = `rotateX(90deg) rotateZ(${yaw}deg) translate3d(0px, 0px, -180px)`;
				yBox.style.transform = `rotateX(${pitch}deg)`;
				xBox.style.transform = `rotateY(${-roll}deg)`;
				attitude = {
					roll,
					pitch,
					yaw
				};
				break;
			case ConfigCmd.SERIAL_PASSTHROUGH | 0x4000:
				const sPort = command.data[0];
				const baud = leBytesToInt(command.data.slice(1, 5));
				console.log(`Serial passthrough started on Serial${sPort} with baud rate ${baud}`);
				port.disconnect();
			case ConfigCmd.GET_GPS_ACCURACY | 0x4000:
				gpsAcc = {
					tAcc: leBytesToInt(command.data.slice(0, 4)) * 1e-3,
					hAcc: leBytesToInt(command.data.slice(4, 8)) * 1e-3,
					vAcc: leBytesToInt(command.data.slice(8, 12)) * 1e-3,
					sAcc: leBytesToInt(command.data.slice(12, 16)) * 1e-3,
					headAcc: leBytesToInt(command.data.slice(16, 20)) * 1e-5,
					pDop: leBytesToInt(command.data.slice(20, 24)) * 0.01
				};
				break;
			case ConfigCmd.GET_GPS_MOTION | 0x4000:
				gpsMotion = {
					lat: leBytesToInt(command.data.slice(0, 4), true) * 1e-7,
					lon: leBytesToInt(command.data.slice(4, 8), true) * 1e-7,
					alt: leBytesToInt(command.data.slice(8, 12), true) * 1e-3,
					velN: leBytesToInt(command.data.slice(12, 16), true) * 1e-3,
					velE: leBytesToInt(command.data.slice(16, 20), true) * 1e-3,
					velD: leBytesToInt(command.data.slice(20, 24), true) * 1e-3,
					gSpeed: leBytesToInt(command.data.slice(24, 28), true) * 1e-3,
					headMot: leBytesToInt(command.data.slice(28, 32), true) * 1e-5
				};
				combinedAltitude = leBytesToInt(command.data.slice(32, 36), true) / 65536;
				verticalVelocity = leBytesToInt(command.data.slice(36, 40), true) / 65536;
				break;
			case ConfigCmd.GET_GPS_STATUS | 0x4000:
				gpsStatus = {
					gpsInited: command.data[0] === 1,
					initStep: command.data[1],
					fix: command.data[2],
					timeValidityFlags: command.data[3],
					flags: command.data[4],
					flags2: command.data[5],
					flags3: leBytesToInt(command.data.slice(6, 8)),
					satCount: command.data[8]
				};
				break;
			case ConfigCmd.GET_GPS_TIME | 0x4000:
				gpsTime = {
					year: leBytesToInt(command.data.slice(0, 2)),
					month: command.data[2],
					day: command.data[3],
					hour: command.data[4],
					minute: command.data[5],
					second: command.data[6]
				};
				break;
			case ConfigCmd.GET_CRASH_DUMP | 0x4000:
				console.log(command.data);
				break;
			case ConfigCmd.CALIBRATE_ACCELEROMETER | 0x4000:
				console.log('Accelerometer calibrated');
				break;
		}
	}

	function degToDegMinSec(deg: number) {
		const d = Math.floor(deg);
		const m = Math.floor((deg - d) * 60);
		const s = Math.round(((deg - d) * 60 - m) * 60);
		return `${d}° ${m}' ${s}"`;
	}

	function ledOn() {
		port.sendCommand(ConfigCmd.SET_DEBUG_LED, [1]);
	}
	function ledOff() {
		port.sendCommand(ConfigCmd.SET_DEBUG_LED, [0]);
	}
	function calibrateAccel() {
		port.sendCommand(ConfigCmd.CALIBRATE_ACCELEROMETER);
	}
	function playSound() {
		port.sendCommand(ConfigCmd.PLAY_SOUND);
	}

	function delay(ms: number) {
		return new Promise((resolve) => {
			setTimeout(resolve, ms);
		});
	}

	let pingInterval = 0;
	onMount(() => {
		getRotationInterval = setInterval(() => {
			port.sendCommand(ConfigCmd.GET_ROTATION).catch(() => {});
		}, 20);
		getGpsData = setInterval(() => {
			port
				.sendCommand(ConfigCmd.GET_GPS_ACCURACY)
				.then(() => {
					return port.sendCommand(ConfigCmd.GET_GPS_MOTION);
				})
				.catch(() => {});
		}, 200);
		gpsDataSlow = setInterval(() => {
			port
				.sendCommand(ConfigCmd.GET_GPS_STATUS)
				.then(() => {
					return port.sendCommand(ConfigCmd.GET_GPS_TIME);
				})
				.catch(() => {});
		}, 1500);
		pingInterval = setInterval(() => {
			pingFromConfigurator = port.getPingTime().fromConfigurator;
			pingFromFC = port.getPingTime().fromFC;
		}, 1000);
	});
	onDestroy(() => {
		clearInterval(getRotationInterval);
		clearInterval(getGpsData);
		clearInterval(gpsDataSlow);
		clearInterval(pingInterval);
	});
</script>

<div>
	<button on:click={() => ledOn()}>LED On</button>
	<button on:click={() => ledOff()}>LED Off</button>
	<button on:click={() => calibrateAccel()}>Calibrate Accelerometer</button>
	<button on:click={() => playSound()}>Play Sound</button>
	<input
		type="number"
		name="serial"
		step="1"
		min="1"
		max="2"
		placeholder="Serial Number"
		bind:value={serialNum}
	/>
	<input
		type="number"
		name="baud"
		step="1"
		min="9600"
		max="115200"
		placeholder="Baud Rate"
		bind:value={baudRate}
	/>
	<button
		on:click={() => {
			port.sendCommand(ConfigCmd.SERIAL_PASSTHROUGH, [
				serialNum,
				baudRate & 0xff,
				(baudRate >> 8) & 0xff,
				(baudRate >> 16) & 0xff,
				baudRate >> 24
			]);
		}}>Start Serial Passthrough</button
	>
	<button
		on:click={() => {
			port.enableCommands(false);
			port
				.sendRaw([], '+++')
				.then(() => {
					return delay(1500);
				})
				.then(() => port.enableCommands(true));
		}}>Stop Serial Passthrough</button
	>
	<button on:click={() => port.sendCommand(ConfigCmd.GET_CRASH_DUMP)}>Get Crash Dump</button>
	<button on:click={() => port.sendCommand(ConfigCmd.CLEAR_CRASH_DUMP)}>Clear Crash Dump</button>
	<button on:click={() => port.sendCommand(ConfigCmd.REBOOT)}>Reboot</button>
	<button on:click={() => port.sendCommand(ConfigCmd.REBOOT_BY_WATCHDOG)}>Reboot (Watchdog)</button>
	<button on:click={() => port.sendCommand(ConfigCmd.REBOOT_TO_BOOTLOADER)}>Bootloader</button>
</div>
<div class="droneStatus">
	Flight Mode: {FLIGHT_MODES[flightMode]}, Armed: {armed ? 'Yes' : 'No'}, Configurator Connected: {configuratorConnected
		? 'Yes'
		: 'No'}<br />
	Arming Disabled Flags:<br />
	{ARMING_DISABLE_FLAGS.map((flag, i) => {
		if (armingDisableFlags & (1 << i)) return flag + ', ';
	}).join('')}
</div>
<div>
	Ping from Configurator: {pingFromConfigurator} - Ping from FC: {pingFromFC}
</div>
<div class="drone3DPreview">
	<div class="zBox droneAxes" bind:this={zBox}>
		<div class="yBox droneAxes" bind:this={yBox}>
			<div class="xBox droneAxes" bind:this={xBox}>
				<div class="droneFrame">
					<div class="flrrBar" />
					<div class="rlfrBar" />
				</div>
				<div class="arrowForward" />
				<div class="props">
					<div class="dronePropellerRR" />
					<div class="dronePropellerFR" />
					<div class="dronePropellerRL" />
					<div class="dronePropellerFL" />
				</div>
			</div>
		</div>
	</div>
</div>
<div class="attInfo">
	<div class="axisLabel axisRoll">Roll: {roundToDecimal(attitude.roll, 2)}°</div>
	<div class="axisLabel axisPitch">Pitch: {roundToDecimal(attitude.pitch, 2)}°</div>
	<div class="axisLabel axisYaw">Yaw: {roundToDecimal(attitude.yaw, 2)}°</div>
	<br />
	Combined Altitude: {roundToDecimal(combinedAltitude, 2)}m<br />
	Vertical Velocity: {roundToDecimal(verticalVelocity, 2)}m/s
</div>
<div class="gpsAcc gpsInfo">
	Time Accuracy: {roundToDecimal(gpsAcc.tAcc, 2)}µs<br />
	Horizontal Accuracy: {roundToDecimal(gpsAcc.hAcc, 2)}m<br />
	Vertical Accuracy: {roundToDecimal(gpsAcc.vAcc, 2)}m<br />
	Heading Accuracy: {roundToDecimal(gpsAcc.headAcc, 2)}°<br />
	Speed Accuracy: {roundToDecimal(gpsAcc.sAcc, 2)}m/s<br />
	PDOP: {roundToDecimal(gpsAcc.pDop, 2)}
</div>
<div class="gpsMotion gpsInfo">
	Latitude: {degToDegMinSec(gpsMotion.lat)} - {roundToDecimal(gpsMotion.lat, 7)}°<br />
	Longitude: {degToDegMinSec(gpsMotion.lon)} - {roundToDecimal(gpsMotion.lon, 7)}°<br />
	Altitude: {roundToDecimal(gpsMotion.alt, 2)}m<br />
	North Velocity: {roundToDecimal(gpsMotion.velN, 2)}m/s<br />
	East Velocity: {roundToDecimal(gpsMotion.velE, 2)}m/s<br />
	Down Velocity: {roundToDecimal(gpsMotion.velD, 2)}m/s<br />
	Ground Speed: {roundToDecimal(gpsMotion.gSpeed, 2)}m/s<br />
	Heading: {roundToDecimal(gpsMotion.headMot, 2)}°
</div>
<div class="gpsStatus gpsInfo">
	GPS Inited: {gpsStatus.gpsInited ? 'Yes' : 'No'}<br />
	Init Step: {gpsStatus.initStep}<br />
	Fix: {gpsStatus.fix}<br />
	Time Validity Flags: {gpsStatus.timeValidityFlags}<br />
	Flags: {gpsStatus.flags}<br />
	Flags2: {gpsStatus.flags2}<br />
	Flags3: {gpsStatus.flags3}<br />
	Satellite Count: {gpsStatus.satCount}
</div>
<div class="gpsTime gpsInfo">
	Year: {gpsTime.year}<br />
	Month: {gpsTime.month}<br />
	Day: {gpsTime.day}<br />
	Hour: {gpsTime.hour}<br />
	Minute: {gpsTime.minute}<br />
	Second: {gpsTime.second}
</div>

<style>
	.drone3DPreview {
		width: 500px;
		height: 500px;
		background-color: rgba(255, 255, 255, 0.4);
		perspective: 600px;
		position: relative;
		display: inline-block;
	}
	.zBox {
		top: -130px;
		transform: rotateX(90deg) rotateY(0deg) translate3d(0px, 0px, -180px);
	}
	/*Rotating the bounding box later via JS will rotate the whole drone with it*/
	.xBox,
	.yBox {
		transform: rotateX(0deg) rotateY(0deg);
	}
	.droneAxes {
		width: 100%;
		height: 100%;
		position: relative;
		transform-style: preserve-3d;
	}
	.flrrBar,
	.rlfrBar {
		width: 350px;
		height: 20px;
		background-color: #000;
		position: absolute;
		transform-style: preserve-3d;
		top: 50%;
		left: 50%;
	}
	.flrrBar {
		transform: translate(-50%, -50%) rotatez(-45deg);
	}
	.rlfrBar {
		transform: translate(-50%, -50%) rotatez(45deg);
	}
	.props {
		transform-style: preserve-3d;
	}
	.props div {
		width: 150px;
		transform-style: preserve-3d;
		height: 150px;
		background-color: #f00;
		position: absolute;
		transform-style: preserve-3d;
		border-radius: 100%;
	}
	.dronePropellerFL {
		transform: translate3d(60px, 60px, 15px);
	}
	.dronePropellerFR {
		transform: translate3d(290px, 60px, 15px);
	}
	.dronePropellerRL {
		transform: translate3d(60px, 290px, 15px);
	}
	.dronePropellerRR {
		transform: translate3d(290px, 290px, 15px);
	}
	.arrowForward {
		width: 100px;
		height: 50px;
		position: absolute;
		left: 50%;
		top: 150px;
		transform: translate3d(-50%, -50%, 10px);
		background-color: green;
		clip-path: polygon(0 100%, 50% 0, 100% 100%);
	}
	.attInfo,
	.gpsInfo {
		display: inline-block;
		margin: 0 1rem;
		min-width: 200px;
	}
	button,
	input {
		color: black;
	}
</style>
