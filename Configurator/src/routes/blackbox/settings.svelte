<script lang="ts">
	import { port, MspFn, MspVersion } from '../../portStore';
	import { leBytesToInt } from '../../utils';
	import { onMount, createEventDispatcher, onDestroy } from 'svelte';
	const dispatch = createEventDispatcher();

	let divider = 0;

	const unsubscribe = port.subscribe(command => {
		if (command.cmdType === 'response') {
			switch (command.command) {
				case MspFn.GET_BB_SETTINGS:
					if (command.length !== 9) break;
					divider = command.data[0];
					const selectedBin = leBytesToInt(command.data.slice(1, 5), false);
					const selectedBin2 = leBytesToInt(command.data.slice(5, 9), false);
					const sel = [];
					for (let i = 0; i < 32; i++) {
						if (selectedBin & (1 << i)) sel.push(flagNames[i]);
						if (selectedBin2 & (1 << i)) sel.push(flagNames[i + 32]);
					}
					selected = sel;
					break;
				case MspFn.SET_BB_SETTINGS:
					port.sendCommand('request', MspFn.SAVE_SETTINGS);
					break;
				case MspFn.SAVE_SETTINGS:
					dispatch('close');
					break;
			}
		}
	});

	export let flags: {
		[key: string]: { name: string };
	};
	let flagNames = [] as string[];

	let selected = [] as string[];

	let groups = [] as string[][];

	const groupSizes = [4, 4, 3, 5, 5, 5, 3, 3, 3, 1, 7];

	onMount(() => {
		flagNames = Object.keys(flags);
		let i = 0;
		const g = [] as string[][];
		for (const size of groupSizes) {
			g.push(flagNames.slice(i, i + size));
			i += size;
		}
		groups = g;
		port.sendCommand('request', MspFn.GET_BB_SETTINGS);
	});
	onDestroy(() => {
		unsubscribe();
	});

	function saveSettings() {
		const bytes = [0, 0, 0, 0, 0, 0, 0, 0];
		for (const name of selected) {
			const index = flagNames.indexOf(name);
			const byte = Math.floor(index / 8);
			const bit = index % 8;
			bytes[byte] |= 1 << bit;
		}

		port.sendCommand('request', MspFn.SET_BB_SETTINGS, MspVersion.V2, [divider, ...bytes]);
	}
	function cancel() {
		dispatch('close');
	}
</script>

<div class="background">
	<div class="settingsWindow">
		{#each groups as group, i}
			<div class="group">
				{#each group as name, j}
					<label for={`check-${name}`}>
						<input
							id={`check-${name}`}
							type="checkbox"
							bind:group={selected}
							name="selected"
							value={name}
						/>
						{flags[name].name}
					</label>
				{/each}
			</div>
		{/each}
		<div class="dividerSetting"><input type="number" bind:value={divider} /></div>
		<div class="apply">
			<button class="saveBtn" on:click={saveSettings}>Save settings</button><button
				class="cancelBtn"
				on:click={cancel}>Cancel</button
			>
		</div>
	</div>
</div>

<style>
	.background {
		position: absolute;
		top: 0;
		left: 0;
		width: 100%;
		height: 100%;
		background-color: #0007;
		z-index: 1;
	}
	.settingsWindow {
		position: absolute;
		top: 50%;
		left: 50%;
		transform: translate(-50%, -50%);
		width: 500px;
		height: 500px;
		min-width: 70%;
		min-height: 70%;
		background-color: var(--background-blue);
		box-sizing: content-box;
		border-radius: 1rem;
		padding: 1rem;
	}
	label {
		display: block;
	}

	.settingsWindow {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
		gap: 1rem;
	}

	.apply {
		grid-column: 1 / -1;
		text-align: right;
	}
	.saveBtn,
	.cancelBtn {
		float: right;
		background-color: transparent;
		border: 1px solid var(--border-green);
		border-radius: 5px;
		padding: 0.5rem 1rem;
		font-size: 1rem;
		color: var(--text-color);
		transition: background-color 0.2s ease-out;
		margin-left: 1rem;
	}
	.saveBtn:hover,
	.cancelBtn:hover {
		background-color: #fff1;
	}
</style>
