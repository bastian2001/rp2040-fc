<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { port, MspFn } from '../../portStore';
	import { leBytesToInt } from '../../utils';

	const TASK_NAMES = [
		'Loop 0',
		'    - Speaker',
		'    - Baro Eval',
		'    - Blackbox',
		'    - ELRS',
		'    - Modes',
		'    - ADC',
		'    - Serial',
		'    - Configurator',
		'    - GPS',
		'    - Magnetometer',
		'    - Task Manager',
		'Loop 1',
		'    - Gyro Read',
		'    - IMU',
		'        - IMU Gyro',
		'        - IMU Accel',
		'        - IMU Angle',
		'        - IMU Speeds',
		'    - PID, Motors',
		'        - ESC RPM',
		'    - OSD',
		'    - Baro Read'
	];

	let tasks = [] as {
		name: string;
		maxDuration: number;
		minDuration: number;
		avgDuration: number;
		frequency: number;
		errorCount: number;
		lastError: number;
		debugInfo: number;
		maxGap: number;
	}[];
	for (let i = 0; i < TASK_NAMES.length; i++) {
		tasks.push({
			name: TASK_NAMES[i],
			maxDuration: 0,
			minDuration: 0,
			avgDuration: 0,
			frequency: 0,
			errorCount: 0,
			lastError: 0,
			debugInfo: 0,
			maxGap: 0
		});
	}

	const unsubscribe = port.subscribe(command => {
		if (command.cmdType === 'response') {
			switch (command.command) {
				case MspFn.TASK_STATUS:
					for (let i = 0; i < tasks.length; i++) {
						tasks[i].debugInfo = leBytesToInt(command.data.slice(i * 32, i * 32 + 4));
						tasks[i].minDuration = leBytesToInt(command.data.slice(i * 32 + 4, i * 32 + 8));
						tasks[i].maxDuration = leBytesToInt(command.data.slice(i * 32 + 8, i * 32 + 12));
						tasks[i].frequency = leBytesToInt(command.data.slice(i * 32 + 12, i * 32 + 16));
						tasks[i].avgDuration = leBytesToInt(command.data.slice(i * 32 + 16, i * 32 + 20));
						tasks[i].errorCount = leBytesToInt(command.data.slice(i * 32 + 20, i * 32 + 24));
						tasks[i].lastError = leBytesToInt(command.data.slice(i * 32 + 24, i * 32 + 28));
						tasks[i].maxGap = leBytesToInt(command.data.slice(i * 32 + 28, i * 32 + 32));
					}
					break;
			}
		}
	});
	let interval: number;
	onMount(() => {
		interval = setInterval(() => {
			port.sendCommand('request', MspFn.TASK_STATUS);
		}, 200);
	});
	onDestroy(() => {
		clearInterval(interval);
		unsubscribe();
	});
</script>

<table>
	<tr>
		<th>Task</th>
		<th>Min Duration</th>
		<th>Max Duration</th>
		<th>Avg Duration</th>
		<th>Frequency</th>
		<th>Error Count</th>
		<th>Last Error</th>
		<th>Debug Info</th>
		<th>Max Gap</th>
	</tr>
	{#each tasks as task, i}
		<tr>
			<td style="white-space:pre">{task.name}</td>
			<td>{task.minDuration}</td>
			<td>{task.maxDuration}</td>
			<td>{task.avgDuration}</td>
			<td>{task.frequency}</td>
			<td>{task.errorCount}</td>
			<td>{task.lastError}</td>
			<td>{task.debugInfo}</td>
			<td>{task.maxGap}</td>
		</tr>
	{/each}
</table>

<style>
	td,
	th {
		text-align: left;
		padding: 3px 20px;
	}
</style>
