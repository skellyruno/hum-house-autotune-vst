# HumHouse Vocal Tune

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

## Signal Flow

```
Input → Input Gain → YIN Pitch Detection → Scale Quantization
     → Note Sustain/Hold → Retune Speed Smoothing
     → TD-PSOLA Pitch Shift → Dry/Wet Mix → Output Gain → Output
```

## Downloadable Binaries

Binaries are produced by the GitHub Actions workflow on every push and release:

| Platform | Formats | Asset |
|----------|---------|-------|
| macOS | Universal `.vst3` + `.component` (AU) + `.app`, guided `.pkg` installer with EULA | `HumHouse-VocalTune-macOS.dmg` / `.pkg` |
| Windows | `.vst3` + Standalone `.exe`, Inno Setup installer with EULA | `HumHouse-VocalTune-Windows-x64.zip` |
| Linux | `.vst3` + Standalone | `HumHouse-VocalTune-Linux-x86_64.zip` |

### Windows Installer

The Inno Setup installer (`installer/humhouse-vocal-tune.iss`) presents a
EULA agreement and installs:
- VST3 to `C:\Program Files\Common Files\VST3\`
- Standalone to `C:\Program Files\HumHouse\HumHouse Vocal Tune\`

### macOS Installer

The `.pkg` installer presents a welcome screen and EULA, then installs:
- VST3 to `/Library/Audio/Plug-Ins/VST3/`
- AU to `/Library/Audio/Plug-Ins/Components/`
- Standalone to `/Applications/`

## Layout

```
.
├── CMakeLists.txt                  # Top-level CMake; fetches JUCE via FetchContent
├── Source/
│   ├── PluginProcessor.{h,cpp}     # APVTS, parameter routing, dry/wet mix
│   ├── PluginEditor.{h,cpp}        # MetaTune-style purple GUI with orb visualizer
│   ├── HumHouseLookAndFeel.h       # Purple/dark theme colours, knob rendering
│   ├── AutoTuneEngine.h            # Combines detection + quantization + shifting
│   ├── PitchDetector.h             # YIN pitch detection with CMND + median filter
│   └── PitchShifter.h              # Period-synchronous TD-PSOLA pitch shifting
├── installer/
│   ├── humhouse-vocal-tune.iss     # Inno Setup script (Windows)
│   └── eula.txt                    # End-User License Agreement
├── scripts/
│   ├── package_macos_dmg.sh        # Build .dmg (drag-to-install)
│   └── package_macos_pkg.sh        # Build guided .pkg installer
└── .github/workflows/build.yml     # CI/CD: build + package for all platforms
```

## Building from Source

```bash
# macOS (universal)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release -j

# Windows (Visual Studio)
cmake -S . -B build -A x64
cmake --build build --config Release -j

# Linux
sudo apt-get install -y libasound2-dev libx11-dev libxrandr-dev \
     libxinerama-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev \
     libgl1-mesa-dev libxrender-dev libxcomposite-dev libxext-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

## Technical Design

### Why TD-PSOLA?

Previous implementations in the HumHouse vocal suite used fixed-size
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
