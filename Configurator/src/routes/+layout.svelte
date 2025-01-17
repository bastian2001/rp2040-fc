<script lang="ts">
	import { onMount, onDestroy, tick } from 'svelte';
	import { page } from '$app/stores';
	import { port, MspFn, MspVersion } from '../portStore';
	import { configuratorLog } from '../logStore';
	import { leBytesToInt, delay } from '../utils';

	let devices: any = [];

	let listInterval: number;

	let battery = '';

	let device: any;
	let connected = false;

	let logDiv: any = null;

	type NavigatorElement = {
		name: string;
		path: string;
		img?: string;
	};

	const navElements: NavigatorElement[] = [
		{
			name: 'Home',
			path: '/'
		},
		{
			name: 'Blackbox',
			path: '/blackbox'
		},
		{
			name: 'Tasks',
			path: '/tasks'
		},
		{
			name: 'OSD',
			path: '/osd'
		},
		{
			name: 'Motors',
			path: '/motors'
		},
		{
			name: 'Receiver',
			path: '/receiver'
		},
		{
			name: 'Tuning',
			path: '/tuning'
		},
		{
			name: 'GPS & Mag',
			path: '/gpsmag'
		}
	];

	const unsubscribeLog = configuratorLog.subscribe(() => {
		tick().then(() => {
			logDiv.scrollTop = logDiv.scrollHeight;
		});
	});

	const unsubscribePort = port.subscribe(command => {
		if (command.cmdType === 'request') {
			switch (command.command) {
				case MspFn.IND_MESSAGE:
					configuratorLog.push(command.dataStr);
					break;
			}
		} else if (command.cmdType === 'response') {
			switch (command.command) {
				case MspFn.API_VERSION:
					configuratorLog.push(
						`Protocol: ${command.data[0]}, API: ${command.data[1]}.${command.data[2]}`
					);
					break;
				case MspFn.FIRMWARE_VARIANT:
					configuratorLog.push(`Firmware: ${command.dataStr}`);
					if (command.dataStr !== 'KOLI') {
						configuratorLog.push(
							`This configurator is only compatible with Kolibri firmware, disconnecting...`
						);
						disconnect();
					}
					break;
				case MspFn.FIRMWARE_VERSION:
					configuratorLog.push(`Version: ${command.data[0]}.${command.data[1]}.${command.data[2]}`);
					break;
				case MspFn.BOARD_INFO:
					configuratorLog.push(
						`Board: ${command.dataStr.substring(0, 4)} => ${command.dataStr.substring(
							9,
							9 + command.data[8]
						)}`
					);
					break;
				case MspFn.BUILD_INFO:
					const date = command.dataStr.substring(0, 11);
					const time = command.dataStr.substring(11, 19);
					const githash = command.dataStr.substring(19);
					configuratorLog.push(`Firmware released: ${date} ${time} (Git: #${githash})`);
					break;
				case MspFn.GET_NAME:
					configuratorLog.push(`Name: ${command.dataStr}`);
					break;
				case MspFn.STATUS:
					battery = `${leBytesToInt(command.data.slice(0, 2)) / 100}V`;
					break;
				case MspFn.SET_RTC:
					configuratorLog.push('RTC updated');
					break;
				case MspFn.SET_TZ_OFFSET:
					configuratorLog.push('Timezone offset updated');
					break;
				case MspFn.PLAY_SOUND:
					console.log(command.data);
					break;
				case MspFn.SAVE_SETTINGS:
					configuratorLog.push('EEPROM saved');
			}
		}
	});

	function listDevices() {
		devices = port.getDevices();
	}
	setTimeout(listDevices, 150);
	function odh() {
		connected = false;
	}
	function och() {
		connected = true;
		configuratorLog.clearEntries();
		port
			.sendCommand('request', MspFn.API_VERSION)
			.then(() => port.sendCommand('request', MspFn.FIRMWARE_VARIANT))
			.then(() => delay(5))
			.then(() => port.sendCommand('request', MspFn.FIRMWARE_VERSION))
			.then(() => delay(5))
			.then(() => port.sendCommand('request', MspFn.BOARD_INFO))
			.then(() => delay(5))
			.then(() => port.sendCommand('request', MspFn.BUILD_INFO))
			.then(() => delay(5))
			.then(() => port.sendCommand('request', MspFn.GET_NAME))
			.then(() => delay(5))
			.then(() => port.sendCommand('request', MspFn.STATUS))
			.then(() => {
				const now = Date.now() / 1000;
				port.sendCommand('request', MspFn.SET_RTC, MspVersion.V2, [
					now & 0xff,
					(now >> 8) & 0xff,
					(now >> 16) & 0xff,
					(now >> 24) & 0xff
				]);
			})
			.then(() => delay(5))
			.then(() => {
				const offset = -new Date().getTimezoneOffset();
				port.sendCommand('request', MspFn.SET_TZ_OFFSET, MspVersion.V2, [
					offset & 0xff,
					(offset >> 8) & 0xff
				]);
			});
	}
	onMount(() => {
		disconnect();
		listInterval = setInterval(() => {
			listDevices();
		}, 1000);
		port.addOnConnectHandler(och);
		port.addOnDisconnectHandler(odh);
	});
	onDestroy(() => {
		clearInterval(listInterval);
		disconnect();
		port.removeOnConnectHandler(och);
		port.removeOnDisconnectHandler(odh);
		unsubscribeLog();
		unsubscribePort();
	});
	function connect() {
		port.connect(device).catch(() => {
			connected = false;
		});
	}
	function disconnect() {
		port.disconnect().catch(() => {
			connected = false;
		});
	}
</script>

<div class="main">
	<div class="header">
		<img
			src="https://svelte.dev/svelte-logo-horizontal.svg"
			alt="Svelte logo"
			class="kolibriLogo"
		/>
		<div class="space" />
		<div class="connector">
			<select bind:value={device}>
				{#each devices as d}
					<option value={d}>{d}</option>
				{/each}
			</select>&nbsp;&nbsp;
			{#if connected}
				<button on:click={() => disconnect()} class="connectButton">Disconnect</button>
			{:else if devices.length > 0}
				<button on:click={() => connect()} class="connectButton">Connect</button>
			{:else}
				<span style="color: #888">No devices found</span>
			{/if}&nbsp;&nbsp;
		</div>
		{#if connected}
			<div class="battery">
				<p>Battery: {battery}</p>
			</div>
		{/if}
		<div class="log" bind:this={logDiv}>
			{#each $configuratorLog as l}<p>{l}</p>{/each}
		</div>
	</div>

	<div class="pageContent">
		<slot />
	</div>
	<div class="navigator">
		<div class="navElement grow" />
		{#each navElements as el}
			<a href={el.path} class="navElement" class:currentPage={$page.route.id === el.path}>
				{#if el.img}
					<img src={el.img} alt={`${el.name} Icon`} class="pageIcon" />
				{/if}
				<p class="pageName">{el.name}</p>
			</a>
		{/each}
		<div class="navElement grow" />
	</div>
</div>

<style>
	.main {
		display: flex;
		flex-direction: column;
		height: 100vh;
		overflow: hidden;
	}

	.header {
		display: flex;
		flex-direction: row;
		align-items: center;
		border-bottom: 3px solid var(--border-blue);
	}
	.kolibriLogo {
		height: 3rem;
	}

	.space {
		flex-grow: 1;
	}

	.connector {
		display: flex;
		flex-direction: row;
		align-items: center;
	}

	.connector select {
		width: 8rem;
		background-color: transparent;
		border: 1px solid var(--border-color);
		border-radius: 4px;
		padding: 3px 6px;
		color: var(--text-color);
		outline: none;
	}
	.connector select option {
		color: #444;
	}

	.connectButton {
		background-color: transparent;
		border: 1px solid var(--border-color);
		border-radius: 4px;
		padding: 4px 8px;
		color: var(--text-color);
		outline: none;
	}
	.log {
		flex-grow: 1;
		min-width: 250px;
		margin-left: 1rem;
		background-color: var(--background-color-light);
		height: 3rem;
		padding: 0 0.5rem;
		color: var(--text-color);
		font-size: 0.7rem;
		line-height: 100%;
		overflow-y: scroll;
	}
	.log p {
		margin: 3px 0;
		padding: 0;
	}

	.navigator {
		display: flex;
		flex-direction: row;
		align-items: center;
		justify-content: center;
	}
	.navElement {
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		padding: 0.5rem;
		border-top: 3px solid var(--border-blue);
		color: var(--text-color);
		text-decoration: none;
		background-color: var(--background-blue);
	}
	.navElement.currentPage {
		border-top: 3px solid transparent;
		background-color: var(--background-color);
	}
	.pageName {
		margin: 0;
		padding: 0;
	}
	.navElement.grow {
		flex-grow: 1;
		height: 100%;
		box-sizing: border-box;
	}
	.pageContent {
		flex-shrink: 1;
		flex-grow: 1;
		overflow: auto;
	}

	.battery p {
		margin: 0;
		padding: 0;
	}
</style>
