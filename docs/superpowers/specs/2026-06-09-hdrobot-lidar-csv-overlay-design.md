# hdRobot LiDAR CSV Overlay Design

Date: 2026-06-09

## Goal

`hydra_capture` should render LiDAR point-cloud coordinates from hdRobot CSV
files onto the output image when explicitly requested by the command line.
Default `run.sh` and default executable behavior must not automatically export
or draw LiDAR points.

## User-Facing Contract

- `--lidar-point-cloud <csv>` reads an existing hdRobot CSV file and overlays
  points on the `color` AOV.
- `--export-lidar-point-cloud <csv>` invokes the active render delegate command
  `exportLidarPointCloud`, then reads that generated CSV and overlays points on
  the `color` AOV.
- If neither flag is present, no CSV is read and no overlay is drawn.
- If an overlay flag is present but the final AOV list does not include
  `color`, rendering continues and prints that the LiDAR overlay was skipped.
- The overlay only modifies the saved `color` image. Other AOV outputs are
  written unchanged.

## CSV Input

The CSV format is the hdRobot `exportLidarPointCloud` output:

```text
frame_id,sensor_name,sensor_index,width,height,point_index,ring_index,beam_index,x,y,z,range_meters,intensity,flags,valid,hit,out_of_range
```

The `x`, `y`, and `z` columns are interpreted as world-space coordinates.
Rows are eligible for overlay only when `valid == 1` and `hit == 1`. The parser
must continue to support quoted CSV fields so `sensor_name` can contain commas
without shifting later columns.

## Render Flow

1. Parse CLI options and renderer JSON.
2. Resolve final AOVs.
3. Initialize Hydra, select renderer plugin, apply renderer settings, select
   AOVs, configure camera, configure viewport, and render.
4. If `--export-lidar-point-cloud <csv>` was provided, call the renderer command
   with `filePath=<csv>`.
5. If either LiDAR CSV option was provided and `color` is in the final AOV list,
   read CSV points from:
   - `--lidar-point-cloud` when present.
   - otherwise `--export-lidar-point-cloud`.
6. When writing AOVs, convert only the `color` render buffer to `Rgba8Image`,
   draw projected LiDAR points into that image, then write it with the normal
   output path policy.

## Projection And Drawing

`HydraCaptureEngine::ConfigureCamera()` owns the camera projection basis. It
must expose the same conformed `GfFrustum` matrices used for rendering:

- `viewMatrix = frustum.ComputeViewMatrix()`
- `projectionMatrix = frustum.ComputeProjectionMatrix()`
- output `width` and `height`

Overlay projection uses OpenUSD row-vector matrix multiplication:

```cpp
GfVec4d(x, y, z, 1.0) * viewMatrix * projectionMatrix
```

Points outside clip space, behind the camera, or with non-finite coordinates
are skipped. Visible points are drawn as cyan square markers using
`--lidar-overlay-point-radius`. The first implementation does not perform depth
occlusion against the rendered depth AOV.

## Scope

In scope:

- Ensure existing explicit CLI overlay behavior is correct and documented.
- Ensure hdRobot-generated CSV can be read and projected onto `color`.
- Keep `filemap.md` current if important `src/` or `run.sh` files change.
- Rebuild the graph after code edits.

Out of scope:

- Automatic overlay from `run.sh` defaults.
- Automatic overlay based solely on `config/plugins/hdRobot/plugin.json`.
- Rendering LiDAR CSV points inside the hdRobot render delegate.
- Depth-aware point occlusion.
- Changing hdRobot CSV schema.

## Verification

Expected verification after implementation:

- Build `hydra_capture`.
- Run a CSV parser or smoke path with an hdRobot-style CSV containing at least
  one valid hit point and one invalid or miss row.
- Run an hdRobot scene with `--export-lidar-point-cloud <csv> --aov color` when
  the local renderer plugin is available.
- Confirm command output reports the CSV path, valid hit point count, and drawn
  point count.
- Confirm default `./run.sh` still does not pass a LiDAR CSV flag unless the
  user supplies one.
