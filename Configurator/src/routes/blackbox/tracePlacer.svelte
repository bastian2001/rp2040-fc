<script lang="ts">
	import { createEventDispatcher, onMount } from 'svelte';
	const dispatch = createEventDispatcher();
	type TraceInGraph = {
		flagName: string;
		color: string;
		strokeWidth: number;
		autoRange: boolean;
		minValue: number;
		maxValue: number;
		modifier: any;
		id: number;
		unit?: string;
		states?: string[];
		decimals?: number;
		displayName?: string;
	};
	export let flags: string[];
	export let autoRange: { min: number; max: number };
	export let flagProps: { [key: string]: { name: string; unit: string } };
	export let genFlagProps: { [key: string]: { name: string; unit: string } };

	let autoRangeOn = true;
	let flagName = '';
	let minValue = 0;
	let maxValue = 1;
	let modifier = '';
	let color = '#000000';
	let autoMin = 0;
	let autoMax = 1;
	const gpsModifier = [
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
			min: -0,
			max: 1000,
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
			min: -50,
			max: 50,
			path: 'vel_n',
			unit: 'm/s',
			decimals: 2
		},
		{
			displayNameShort: 'Vel E',
			displayName: 'Velocity East',
			min: -50,
			max: 50,
			path: 'vel_e',
			unit: 'm/s',
			decimals: 2
		},
		{
			displayNameShort: 'Vel D',
			displayName: 'Velocity Down',
			min: -50,
			max: 50,
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
	];

	$: {
		if (autoRange.min !== autoMin) autoMin = autoRange.min;
		if (autoRange.max !== autoMax) autoMax = autoRange.max;
	}

	let traceInGraph: TraceInGraph = {} as TraceInGraph;
	$: {
		traceInGraph.flagName = flagName;
		traceInGraph.color = color;
		traceInGraph.autoRange = autoRangeOn;
		traceInGraph.minValue = autoRangeOn ? autoMin || 0 : minValue;
		traceInGraph.maxValue = autoRangeOn ? autoMax || 1 : maxValue;
		minValue = traceInGraph.minValue;
		maxValue = traceInGraph.maxValue;
		traceInGraph.decimals = gpsModifier.find((m) => m.path === modifier)?.decimals;
		traceInGraph.unit = gpsModifier.find((m) => m.path === modifier)?.unit;
		traceInGraph.states = gpsModifier.find((m) => m.path === modifier)?.states;
		traceInGraph.displayName = gpsModifier.find((m) => m.path === modifier)?.displayName;
		if (traceInGraph.displayName)
			traceInGraph.displayName = flagProps[flagName].name + ' ' + traceInGraph.displayName;
		traceInGraph.modifier = modifier;
		dispatch('update', traceInGraph);
	}

	$: flagName, (modifier = '');
	$: {
		const auto = {
			min: gpsModifier.find((m) => m.path === modifier)?.min,
			max: gpsModifier.find((m) => m.path === modifier)?.max
		};
		if (auto.min !== undefined && auto.max !== undefined) dispatch('overrideAuto', auto);
		else dispatch('overrideAuto', undefined);
	}

	onMount(() => {
		const h = Math.random() * 360;
		const s = Math.random() * 0.5 + 0.5;
		const l = Math.random() * 0.5 + 0.3; // 0.3 - 0.8
		color = `hsl(${h}, ${s * 100}%, ${l * 100}%)`;
	});
</script>

<div class="wrapper">
	<span class="colorMark" style:background-color={color}>&nbsp;</span>
	<select name="flag" id="flagSelector" bind:value={flagName}>
		{#each flags.filter((f) => {
			return f.startsWith('LOG_');
		}) as flag}
			<option value={flag}>{flagProps[flag].name}</option>
		{/each}
		{#each flags.filter((f) => {
			return f.startsWith('GEN_');
		}) as flag}
			<option value={flag}>{genFlagProps[flag].name} (Gen.)</option>
		{/each}
	</select>
	{#if flagName === 'LOG_MOTOR_OUTPUTS' || flagName === 'GEN_MOTOR_OUTPUTS' || flagName === 'LOG_MOTOR_RPM'}
		<select name="graphNum" id="graphNum" bind:value={modifier} style="width: auto">
			{#each ['RR', 'FR', 'RL', 'FL'] as m}
				<option value={m}>{m}</option>
			{/each}
		</select>
	{/if}
	{#if flagName === 'LOG_GPS'}
		<select name="gpsModifier" id="gpsModifier" bind:value={modifier} style="width: auto">
			{#each gpsModifier as m}
				<option value={m.path}>{m.displayNameShort}</option>
			{/each}
		</select>
	{/if}
	{#if flagName === 'LOG_ACCEL_RAW' || flagName === 'LOG_ACCEL_FILTERED'}
		<select name="accelModifier" id="accelModifier" bind:value={modifier} style="width: auto">
			{#each ['X', 'Y', 'Z'] as m}
				<option value={m}>{m}</option>
			{/each}
		</select>
	{/if}
	<button
		class="delete"
		on:click={() => {
			dispatch('delete');
		}}>Del</button
	>
	<br />
	<label><input type="checkbox" bind:checked={autoRangeOn} />Auto </label>&nbsp;
	<input
		type="number"
		name="minValue"
		id="minValue"
		bind:value={minValue}
		disabled={autoRangeOn}
	/>&nbsp;-
	<input type="number" name="maxValue" id="maxValue" bind:value={maxValue} disabled={autoRangeOn} />
	&nbsp;
	<p class="unit">{flagProps[flagName]?.unit || genFlagProps[flagName]?.unit || ''}</p>
</div>

<style>
	.wrapper {
		line-height: 180%;
		margin-bottom: 0.5rem;
	}
	select {
		width: 8rem;
		background-color: transparent;
		border: 1px solid var(--border-color);
		border-radius: 4px;
		padding: 3px 6px;
		color: var(--text-color);
		outline: none;
		text-transform: capitalize;
	}
	option {
		text-transform: capitalize;
		color: #444;
	}
	input[type='number'] {
		width: 3rem;
		background-color: transparent;
		border: 1px solid var(--border-color);
		border-radius: 4px;
		padding: 3px 6px;
		color: var(--text-color);
		outline: none;
		text-transform: capitalize;
	}
	input[disabled] {
		opacity: 0.5;
		cursor: not-allowed;
	}
	.colorMark {
		display: inline-block;
		width: 1.8rem;
		transform: scale(0.8);
		margin: 0 0.2rem;
	}
	.delete {
		background-color: transparent;
		border: 1px solid var(--border-color);
		border-radius: 4px;
		padding: 4px 8px;
		color: var(--text-color);
		outline: none;
		float: right;
		transform: translateY(2px);
	}
	.unit {
		display: inline-block;
		margin: 0;
	}
</style>
