# hydra_capture

`hydra_capture` 用一个 Hydra render delegate 渲染单个 USD stage，并为每个
请求的 AOV 写出一个图片文件。渲染器选择、默认 AOV、输出扩展名和 renderer
setting 都来自 renderer JSON 配置。

## 构建

项目使用 CMake 和 OpenUSD。如果 CMake 不能自动找到 OpenUSD，请先设置
`PXR_DIR`。

```bash
cmake -S . -B build-codex
cmake --build build-codex
```

常用目标：

```bash
./build-codex/hydra_capture --renderer-config config/plugins/hdStorm/plugin.json --usd /path/to/scene.usd --output-dir /tmp/hydra_capture
```

## 命令行接口

必填参数：

```text
--renderer-config <plugin.json>  Renderer JSON 配置。
--usd <scene.usd>                要渲染的单个 USD/USDZ/USDC stage。
--output-dir <dir>               输出根目录。
```

可选参数：

```text
--camera <SdfPath>       Camera prim 路径，例如 /World/Camera。
--disableCameraLight     关闭默认跟随相机的 headlight。
--width <int>            渲染宽度。默认值：1280。
--height <int>           渲染高度。默认值：720。
--aov <token>            要输出的 AOV token，可重复指定。
--max-iterations <int>   渲染循环上限。默认值：1。
--lidar-point-cloud <csv>
                          读取 hdRobot 导出的 LiDAR CSV，并叠加到 color AOV。
--export-lidar-point-cloud <csv>
                          通过 renderer command 导出 LiDAR CSV，再叠加到 color AOV。
--lidar-overlay-point-radius <int>
                          LiDAR 叠加点半径，单位像素。默认值：2。
```

工具不接受 `--renderer-plugin` 和旧的位置参数。Renderer plugin token 必须来自
`--renderer-config` 指向的 JSON。

## 示例：输出 AOV 图片

`run.sh` 是本地运行脚本。默认使用 `config/plugins/hdStorm/plugin.json`，渲染
脚本里的 `DEFAULT_USD`，并把图片写到 `output/aovs/hdStorm/<usd-stem>/`：

```bash
./run.sh
```

也可以显式指定 USD、renderer plugin 和输出目录：

```bash
./run.sh /path/to/scene.usd hdRobot /tmp/hydra_capture
```

第二个参数可以是 `config/plugins/<name>/plugin.json` 这类 JSON 路径，也可以是
`hdStorm`、`hdRobot` 这类配置目录名。额外参数会直接透传给
`hydra_capture`，例如只输出 color：

```bash
./run.sh /path/to/scene.usd hdStorm /tmp/hydra_capture --aov color
```

## 示例：LiDAR 点云叠加到 color

如果已经有 hdRobot `exportLidarPointCloud` command 导出的 CSV，可以在写出
`color` AOV 前叠加点云：

```bash
./build-codex/hydra_capture \
  --renderer-config config/plugins/hdRobot/plugin.json \
  --usd /path/to/scene.usd \
  --output-dir /tmp/hydra_capture \
  --aov color \
  --lidar-point-cloud /path/to/lidar_point_cloud.csv
```

也可以让当前渲染进程先调用 hdRobot renderer command 导出 CSV，再使用该 CSV
叠加。这个参数只对支持 `exportLidarPointCloud` command 的 render delegate 有意义：

```bash
./build-codex/hydra_capture \
  --renderer-config config/plugins/hdRobot/plugin.json \
  --usd /path/to/scene.usd \
  --output-dir /tmp/hydra_capture \
  --aov color \
  --export-lidar-point-cloud /tmp/hydra_capture/lidar_point_cloud.csv
```

LiDAR CSV 的 `x,y,z` 字段按 world-space 坐标处理。叠加只修改 `color` AOV；
其他 AOV 仍按原逻辑写出。初版不做深度遮挡，只把投影到输出相机视野内的
`valid && hit` 点用青色方块覆盖到图片上。

按 renderer 配置里的默认 AOV，输出会写到：

```text
<output-dir>/<usd-stem>/color.png
<output-dir>/<usd-stem>/depth.ppm
<output-dir>/<usd-stem>/primId.ppm
<output-dir>/<usd-stem>/lidar_pointCloud.exr
```

## Renderer 配置

Renderer 配置位于 `config/plugins/`。C++ 当前支持的字段如下：

```json
{
  "name": "hdStorm",
  "renderer_plugin": "HdStormRendererPlugin",
  "defaults": {
    "aovs": ["color", "depth", "primId"],
    "settings": {
      "enableSceneMaterials": true,
      "enableSceneLights": true
    }
  },
  "aovs": {
    "color": { "output_ext": ".png" },
    "depth": { "output_ext": ".ppm" },
    "primId": { "output_ext": ".ppm" }
  }
}
```

字段规则：

- `renderer_plugin` 必填，会传给 `UsdImagingGLEngine::SetRendererPlugin`。
- `name` 是可选元信息。
- `defaults.aovs` 可选。如果命令行没有传 `--aov` 且该列表为空，工具回退到
  `color`。
- `defaults.settings` 可选。这里的值会通过
  `UsdImagingGLEngine::SetRendererSetting` 应用到 render delegate。
- `aovs.<aov>.output_ext` 可选。`color` 默认 `.png`，其他 AOV 默认 `.ppm`。
- 未识别字段会被 C++ loader 忽略。

Renderer setting 支持 JSON bool、int、real、string。若 render delegate 暴露
setting descriptor 默认值，数值和字符串会按 descriptor 类型转换，例如
`float`、`double` 或 `TfToken`。

## 输出路径

输出路径固定为：

```text
<output-dir>/<usd-stem>/<sanitized-aov><output-ext>
```

示例：

```text
/tmp/hydra_capture/redbox_right/color.png
/tmp/hydra_capture/redbox_right/depth.ppm
/tmp/hydra_capture/redbox_right/lidar_pointCloud.exr
```

AOV 文件名会保留 ASCII 字母、数字、`_`、`-` 和 `.`。其他字符会替换为 `_`，
连续非法字符会压缩成一个 `_`。例如 `lidar:pointCloud` 会变成
`lidar_pointCloud`。

## 基础模块

- `src/options.*`：命令行契约模块。负责填充 `Options`，校验必填参数，校验
  宽、高和迭代次数是正整数，并输出 usage。
- `src/renderer_config.*`：renderer JSON 读取模块。使用 OpenUSD
  `pxr/base/js` 解析 JSON，提取 renderer plugin token、默认 AOV、renderer
  settings 和各 AOV 的输出扩展名。
- `src/aov_output.*`：AOV 输出规则模块。负责解析最终 AOV 列表、选择输出扩展名、
  把 AOV token 转成安全文件名，并生成输出路径。
- `src/renderer_settings.*`：renderer setting 应用模块。把 JSON 值转换成
  `pxr::VtValue`，再设置到 `UsdImagingGLEngine`。
- `src/hydra_capture_engine.*`：Hydra runtime 封装模块。负责 GL context、
  `UsdImagingGLEngine`、renderer plugin/AOV/camera/viewport/render 调用，并保存
  当前输出相机的 view/projection/尺寸供 CPU overlay 使用。
- `src/image_output.*`：图像输出模块。把 Hydra render buffer 转成 top-left
  origin 的 `Rgba8Image`，并写出 PNG/PPM 等 Hio 支持的图片格式。
- `src/lidar_point_cloud.*`：hdRobot LiDAR CSV 读取模块。解析 quoted CSV 字段，
  只输出 `valid && hit` 的 world-space 点。
- `src/lidar_overlay.*`：LiDAR CPU overlay 模块。使用当前输出相机矩阵把
  world-space 点投影到 color 图像并覆盖绘制。
- `src/main.cpp`：可执行程序集成层。负责创建 GL context、打开 USD stage、
  选择相机、配置 renderer、执行渲染、按需导出/读取 LiDAR CSV、把点云叠加到
  color AOV，并写出图片文件。

## 渲染流程

1. 解析命令行参数。
2. 读取 renderer JSON。
3. 从 CLI 覆盖、配置默认值或 `color` 回退值解析最终 AOV 列表。
4. 创建 `<output-dir>/<usd-stem>`。
5. 创建 GL context 并打开 USD stage。
6. 使用 `--camera` 指定的相机、stage 上第一个相机，或生成默认相机，并默认添加
   跟随当前相机的 headlight；可用 `--disableCameraLight` 关闭。
7. 配置 `UsdImagingGLEngine` 的 renderer plugin、settings、AOV、渲染尺寸和
   framing。
8. 渲染到收敛或达到 `--max-iterations`。
9. 如果传入 `--export-lidar-point-cloud`，调用当前 render delegate 的
   `exportLidarPointCloud` command 导出 CSV。
10. 如果传入 LiDAR overlay 参数且最终 AOV 列表包含 `color`，读取 CSV 中的
    `valid && hit` 点。
11. 读取每个 AOV render buffer。`color` AOV 在写图前可叠加 LiDAR 点云，其他
    AOV 直接写到对应输出路径。
