# SkellyTune

Dedicated pitch correction (autotune) plugin built with
[JUCE](https://juce.com). Thick vocal sustain, sharp note snapping,
and formant-preserving TD-PSOLA pitch shifting.

Inspired by Slate Digital MetaTune, Antares Auto-Tune, and
Soundtoys Little AlterBoy.

Ships as:

- **VST3 plugin** — FL Studio, Ableton Live, Reaper, Cubase, Studio One, Bitwig
- **AU plugin** (macOS only) — Logic Pro, GarageBand
- **Standalone app** — runs without a DAW

## Features

| Feature | Description |
|---------|-------------|
| **YIN Pitch Detection** | High-accuracy monophonic pitch tracker with median filtering and confidence scoring |
| **TD-PSOLA Pitch Shifting** | Period-synchronous overlap-add for artifact-free correction that preserves vocal thickness |
| **Formant Preservation** | Keeps natural vocal character even with aggressive pitch shifts |
| **Scale Snap** | Major, Minor, Chromatic, or per-note custom scale with root key selection |
| **Retune Speed** | 0 ms (hard T-Pain snap) to 150 ms (gentle natural correction) |
| **Note Sustain** | Configurable cent threshold — once locked to a note, holds as long as vocal stays in range |
| **Note Stabilizer** | Ignores micro-fluctuations to prevent jittery correction |
| **Humanize** | Reduces correction amount for natural imperfection |
| **Low Latency Mode** | Reduced analysis window for live performance |
| **Pitch Orb Display** | Real-time visual feedback showing detected note and correction direction |
| **In/Out Heatmap** | Scrolling pitch history showing detected vs corrected pitch |
| **12 Presets** | Hard Snap, Sharp Correct, Natural, R&B Smooth, Trap Vocal, Gospel, and more |
| **Purple UI** | HumHouse signature dark purple aesthetic inspired by MetaTune |



## Technical Design

### Why TD-PSOLA?

Previous implementations used fixed-size
grains (256 samples) for pitch shifting, which caused:
- Phase discontinuities at grain boundaries
- Metallic / robotic artifacts
- Formant shifting (chipmunk / barrel effect)

TD-PSOLA (Time-Domain Pitch-Synchronous Overlap-Add) solves all three:
1. **Grains are period-length** — aligned to the detected fundamental
2. **Hann-windowed overlap-add** — seamless transitions, no clicks
3. **Pitch changed via placement rate** — grain content (formants) stays intact

### Note Sustain Logic

Once the engine locks onto a scale note, it holds that note as long as
the incoming vocal stays within a configurable cent threshold (default
50 cents). This prevents jittery switching between adjacent notes on
sustained vocals and is key to getting that "painted" autotune sound.

## License

See [installer/eula.txt](installer/eula.txt).
