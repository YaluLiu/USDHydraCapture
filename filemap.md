# File Map

This map covers the project files that are most useful when changing runtime
behavior, tests, or the local run entrypoint. It intentionally omits
configuration files under `config/`.

## Entrypoint

- `run.sh`: Local convenience wrapper for running `hydra_capture` directly or
  through the Python batch runner. It defines the default renderer plugin,
  capture binary, renderer/test config paths, default USD asset, shared output
  directory, and shared render dimensions. With no subcommand it runs `batch`;
  `single`, `batch`, `dataset`, `baseline`, and `test` are callable subcommands.

## `src/`

- `src/README.zh.md`: Chinese developer guide for building `hydra_capture`,
  using the command-line interface, understanding renderer JSON fields, and
  following the high-level render/output flow.
- `src/hydra_capture.cpp`: Main C++ executable integration layer. It parses
  options, loads renderer config, opens the USD stage, chooses or creates a
  camera, initializes the platform GL context, configures
  `UsdImagingGLEngine`, renders requested AOVs, converts Hydra render buffers
  into RGBA8 pixels, and writes image files.
- `src/options.h`: Declares the `Options` command-line data structure plus
  parser and usage-printing functions.
- `src/options.cpp`: Implements the command-line contract for
  `hydra_capture`, including required flags, repeated `--aov` handling, positive
  integer validation for dimensions/iterations, and rejection of unknown
  arguments.
- `src/renderer_config.h`: Declares renderer configuration data structures:
  renderer metadata, plugin token, default AOVs, renderer settings, and per-AOV
  output extensions.
- `src/renderer_config.cpp`: Loads renderer JSON with OpenUSD `Js` APIs,
  validates supported field types, extracts defaults/settings/AOV metadata, and
  reports path-aware parse or schema errors.
- `src/aov_output.h`: Declares helpers for resolving final AOV lists, choosing
  output extensions, sanitizing AOV tokens for filenames, and building output
  paths.
- `src/aov_output.cpp`: Implements output rules shared by rendering and tests:
  CLI AOVs override config defaults, `color` defaults to `.png`, other AOVs
  default to `.ppm`, unsafe filename characters collapse to `_`, and output
  paths use `<output-dir>/<usd-stem>/<aov><ext>`.
- `src/renderer_settings.h`: Declares conversion and application helpers for
  renderer settings read from JSON.
- `src/renderer_settings.cpp`: Converts JSON bool/int/real/string values to
  `pxr::VtValue`, using renderer setting descriptor defaults to preserve float,
  double, and `TfToken` types before applying settings to
  `UsdImagingGLEngine`.

## `tests/`

- `tests/test_cli_config_paths.cpp`: C++ assertion-based unit test executable
  for command-line parsing, renderer config loading, AOV output rules, and
  renderer setting type conversion.
- `tests/test_hydra_batch.py`: Python unittest suite for the batch workflow and
  comparison helpers. It covers test config loading, dataset expansion, command
  construction, expected output paths, dry runs, missing outputs, workflow
  baseline/test comparison behavior, argument parsing, and `run.sh` defaults.
