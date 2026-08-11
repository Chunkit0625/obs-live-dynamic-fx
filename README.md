# OBS Live Dynamic Visual & Audio FX

An OBS Studio 32.x C++ filter plugin containing:

- `Live Dynamic Visual FX`: a single-pass GPU effect implemented in HLSL-style
  OBS `.effect` syntax. It provides mesh warp, breath scaling/offset, tilt,
  smooth HSV/gamma motion, and optional film grain.
- `Live Dynamic Audio FX`: an allocation-free, planar-float audio callback with
  a lightweight granular pitch shifter, 60 Hz / 15 kHz shelving-style EQ, and
  optional pink ambient noise.

All effects are disabled by default so adding a filter does not immediately
alter a live source. Numeric defaults are prefilled with the requested values.

## Project layout

```
src/dynamic-visual-filter.cpp  OBS video filter + property UI
data/dynamic_visual.effect     GPU shader pass
src/dynamic-audio-filter.cpp   OBS audio filter + DSP + property UI
data/locale/                   English and Simplified Chinese UI strings
```

## Build on Windows for OBS 32

1. Install Visual Studio 2022 with the Desktop C++ workload and CMake 3.28+.
2. Obtain an OBS Studio 32 development build, or build OBS from source, so its
   CMake package files and `libobs` import library are available.
3. Configure, replacing `C:/obs-build` with the directory that contains the
   exported OBS CMake package:

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
     -DCMAKE_PREFIX_PATH="C:/obs-build"
   cmake --build build --config RelWithDebInfo
   ```

4. Stage the normal OBS plugin layout:

   ```powershell
   cmake --install build --config RelWithDebInfo --prefix "C:/obs-plugin-stage"
   ```

   Copy the resulting `obs-plugins/64bit/obs-live-dynamic-fx.dll` and
   `data/obs-plugins/obs-live-dynamic-fx/` into the matching folders under the
   OBS Studio installation. Restart OBS, then add the visual filter to a video
   source and the audio filter to an audio source.

## GitHub Actions release ZIP

The repository includes `.github/workflows/release-windows.yml`. It creates an
unsigned Windows x64 ZIP against OBS Studio `32.0.4`, the latest maintenance
release in the 32.0 series when this workflow was added.

1. Create an empty GitHub repository and push this project to its default
   branch.
2. Open **Actions** → **Package OBS Windows Plugin** → **Run workflow**. After
   the job finishes, download the ZIP from the run's **Artifacts** section.
3. To publish the same ZIP as a GitHub Release, push a version tag:

   ```powershell
   git tag v0.1.0
   git push origin v0.1.0
   ```

The release ZIP contains `obs-plugins/64bit/obs-live-dynamic-fx.dll` and its
matching `data/obs-plugins/obs-live-dynamic-fx/` folder. Extract it directly
into the OBS Studio installation directory, then restart OBS. The workflow does
not sign the DLL, so Windows SmartScreen may show a standard unsigned-binary
warning until you add code signing.

## Implementation notes

- The visual filter runs entirely on the graphics backend. `video_tick` only
  advances a scalar time value; no frame pixels cross the CPU/GPU boundary.
- The shader uses a single texture read for the source, plus ALU calculations.
  Film grain uses a small hash-based near-Gaussian approximation instead of a
  texture upload.
- The audio callback performs no heap allocation, locks, disk I/O, or logging.
  It processes OBS' planar float buffers in place.
- The pitch component is intentionally a compact granular time-domain engine
  suitable for ±50 cents artistic motion. For transparent vocal-quality pitch
  correction, replace `granular_pitch_shift` with a dedicated phase-vocoder or
  a licensed DSP library while preserving the same OBS filter interface.
- Measure GPU time and audio render time on the target resolution, GPU, sample
  rate, and channel layout; low overhead is a design objective, not a hardware-
  independent performance guarantee.

## OBS API basis

The project follows OBS' recommended effect-filter flow:
`obs_source_process_filter_begin()` → set effect parameters →
`obs_source_process_filter_end()`. The audio implementation follows the
`filter_audio` callback contract and modifies the supplied audio buffer in
place.
