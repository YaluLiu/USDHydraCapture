#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

OUTPUT_DIR="output"
HYDRA_CAPTURE="./build-codex/hydra_capture"
WIDTH="3840"
HEIGHT="2160"

PLUGIN_NAME="hdRobot" # hdStorm hdRobot
RENDERER_CONFIG="config/plugins/${PLUGIN_NAME}/plugin.json"

DEFAULT_USD="/home/yalu/docker/assets/tile/pao/tile_pao.usd"
DEFAULT_USD="/home/yalu/docker/assets/demo5/World0.usd"


mkdir -p "$OUTPUT_DIR"

function aovs() {
  ./build-codex/hydra_capture \
    --renderer-config config/plugins/${PLUGIN_NAME}/plugin.json \
    --usd ${DEFAULT_USD} \
    --output-dir output/world0 \
    --aov color \
    --export-lidar-point-cloud output/lidar_json/lidar_point_cloud.csv
}

function build() {
  cmake --build build-codex --target hydra_capture
}

if (($# > 0)) && declare -f -- "$1" > /dev/null; then
  "$1" "${@:2}"
else
  aovs "$@"
fi
