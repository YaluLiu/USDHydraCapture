# File Map

This map covers the files most useful for changing runtime behavior, build
behavior, and the local run entrypoint. It intentionally omits configuration
file contents under `config/`; use the renderer JSON schema notes in
`src/README.zh.md` when editing those files.

## Project Shape

- `src/`: C++17 implementation of the `hydra_capture` executable. The runtime
  path parses CLI options, loads a renderer JSON, opens one USD stage, configures
  `UsdImagingGLEngine`, renders requested AOVs, and writes image files.
- `config/`: Renderer plugin JSON files consumed at runtime. Contents are
  deliberately not expanded in this filemap.
- `build-codex/`, `build/`, `cmake-build-*/`: Generated CMake build trees.
- `output/`, `resources/`, `media/`: Runtime/generated output locations.
- `graphify-out/`: Generated code graph artifacts. `GRAPH_REPORT.md` currently
  identifies `ConfigureCamera()`, `SetError()`, `JoinTokens()`,
  `MakeGLContextCurrent()`, renderer plugin/AOV setup, and `Render()` as central
  runtime nodes.
- `tests/`: Not present in the current tree.

## Root Files

- `AGENTS.md`: Local agent instructions. Requires reading
  `graphify-out/GRAPH_REPORT.md` before architecture/codebase answers, rebuilding
  the graph after code-file edits, and keeping this filemap current for important
  `src/`, `tests/`, or `run.sh` changes.
- `.gitignore`: Excludes build trees, graph output, runtime image output, editor
  state, Python caches, and `task.md`.
- `README.md`: Language selector pointing to the Chinese README.
- `README.zh.md`: Language selector pointing to the English README.
- `src/README.zh.md`: Main project documentation. Describes build commands, CLI
  flags, renderer JSON fields, output path rules, module responsibilities, and
  the end-to-end render flow.
- `CMakeLists.txt`: Builds the `hydra_capture` executable from all `src/*.cpp`
  files. Finds OpenGL and OpenUSD `pxr`, honors `PXR_DIR`, links OpenUSD imaging,
  USD, JSON, Hio, GL, and platform libraries.
- `run.sh`: Local convenience wrapper. Defaults to `hdRobot`, renders
  `/home/yalu/docker/assets/tile/pao/tile_pao.usd`, writes under
  `output/aovs/<plugin>/`, uses `3840x2160`, supports `build`, and otherwise
  calls `hydra_capture` with a renderer config, USD path, output dir, and any
  extra CLI flags.
- `filemap.md`: This file.

## Runtime Flow

1. `run.sh` optionally builds or invokes `build-codex/hydra_capture`.
2. `src/main.cpp` calls `ParseArgs()` from `src/options.cpp`.
3. `src/main.cpp` loads renderer JSON through `LoadRendererConfig()` from
   `src/renderer_config.cpp`.
4. Final AOVs and output paths are resolved by `src/aov_output.cpp`.
5. `HydraCaptureEngine::Initialize()` creates a platform GL context and a
   `UsdImagingGLEngine`.
6. `src/main.cpp` opens the USD stage, selects the renderer plugin, applies
   renderer settings, sets AOVs, configures camera and viewport, then renders.
7. Each AOV render buffer is fetched from `HydraCaptureEngine` and passed to
   `WriteRenderBufferImage()` in `src/image_output.cpp`.
8. Saved image paths are printed to stdout.

## `src/`

- `src/main.cpp`: Executable integration layer. Owns the high-level control
  flow: parse options, load config, resolve AOVs, create output directories,
  initialize Hydra, open the USD stage, configure renderer/settings/AOV/camera,
  run rendering, write AOV images, and report saved paths or failures.
- `src/options.h`: Declares the `Options` data structure and the CLI parser and
  usage printer.
- `src/options.cpp`: Implements the CLI contract. Requires
  `--renderer-config`, `--usd`, and `--output-dir`; accepts `--camera`,
  `--disableCameraLight`, repeated `--aov`, positive `--width`, positive
  `--height`, and positive `--max-iterations`; rejects unknown flags.
- `src/renderer_config.h`: Declares renderer JSON data structures:
  `AovConfig`, `RendererConfig`, default AOVs, renderer setting values, and
  per-AOV output extensions.
- `src/renderer_config.cpp`: Parses renderer JSON with OpenUSD `Js` APIs.
  Validates root/defaults/AOV object shapes, requires `renderer_plugin`, accepts
  optional `name`, `defaults.aovs`, `defaults.settings`, and
  `aovs.<name>.output_ext`, and emits path-aware parse/schema errors.
- `src/aov_output.h`: Declares helpers for final AOV selection, output extension
  selection, safe filename token creation, and final AOV output path generation.
- `src/aov_output.cpp`: Implements AOV output rules. CLI AOVs override renderer
  defaults, renderer defaults override the fallback `color`; `color` defaults to
  `.png`, other AOVs default to `.ppm`; unsafe filename characters collapse to
  `_`; paths are `<output-dir>/<usd-stem>/<sanitized-aov><ext>`.
- `src/renderer_settings.h`: Declares JSON-to-`VtValue` conversion and renderer
  setting application helpers for `UsdImagingGLEngine`.
- `src/renderer_settings.cpp`: Converts JSON bool/int/real/string settings to
  `pxr::VtValue`. Uses renderer descriptor default types to preserve `float`,
  `double`, and `TfToken` where appropriate, then calls
  `UsdImagingGLEngine::SetRendererSetting`.
- `src/hydra_capture_engine.h`: Declares `HydraCaptureEngine`, the main runtime
  wrapper around GL context ownership, `UsdImagingGLEngine` ownership, renderer
  plugin selection, setting application, AOV selection, camera setup, viewport
  setup, render execution, render-buffer access, and available-AOV formatting.
- `src/hydra_capture_engine.cpp`: Implements the Hydra runtime wrapper. Contains
  platform GL context creation/destruction for Windows and non-Windows builds,
  `UsdImagingGLEngine` lifetime ordering, renderer plugin/AOV error reporting,
  first-camera discovery, generated default camera framing from scene bounds,
  camera-positioned headlight setup, viewport/framing setup, default render
  params, render iteration until convergence or max iterations, and AOV buffer
  lookup.
- `src/image_output.h`: Declares the image writing entry point used by
  `src/main.cpp`.
- `src/image_output.cpp`: Converts Hydra `HdRenderBuffer` data to RGBA8 pixels
  and writes output images. Handles mapping/unmapping with RAII, vertical image
  orientation, common UNorm8/Float16/Float32/Int32/depth-stencil formats,
  deterministic ID visualization, direct `.ppm` writing, and other formats via
  OpenUSD `HioImage`.

## Ownership And Boundaries

- `src/main.cpp` owns process-level sequencing and error exits.
- `HydraCaptureEngine` owns GL context and `UsdImagingGLEngine` lifetime. The GL
  context is destroyed after the engine is reset.
- `renderer_config.*` owns file/schema parsing only; it does not apply settings
  or validate renderer plugin availability.
- `renderer_settings.*` owns conversion from parsed JSON values to runtime
  OpenUSD setting values.
- `aov_output.*` owns naming and path policy, independent of Hydra.
- `image_output.*` owns render-buffer mapping, format conversion, and image I/O.

## Change Hotspots

- CLI behavior: update `src/options.*`, then keep `src/README.zh.md`, `run.sh`
  examples, and this filemap aligned.
- Renderer JSON schema or defaults: update `src/renderer_config.*`,
  `src/renderer_settings.*` if setting typing changes, and `src/README.zh.md`.
- AOV naming, extension, or directory layout: update `src/aov_output.*`,
  `src/main.cpp` if call sequencing changes, and documentation.
- Camera, lighting, viewport, render loop, or renderer plugin behavior: update
  `src/hydra_capture_engine.*`.
- Image format support or visualization: update `src/image_output.*`.
- Build dependencies or source list: update `CMakeLists.txt`.
- Local execution defaults: update `run.sh`.
