#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

OUTPUT_DIR="output"
PLUGIN_NAME="hdRobot"
HYDRA_CAPTURE="./build-codex/hydra_capture"
RENDERER_CONFIG="config/plugins/${PLUGIN_NAME}/plugin.json"
TEST_CONFIG="config/test/ubuntu.json"
DEFAULT_USD="/home/yalu/docker/assets/unit_test/single/four_box/mesh_output_1761207339.usdc"
DEFAULT_USD="/home/yalu/docker/assets/obj/four_box.usdz"
WIDTH="3840"
HEIGHT="2160"

mkdir -p "$OUTPUT_DIR"

function single() {
  local usd_path="${1:-$DEFAULT_USD}"

  "$HYDRA_CAPTURE" \
    --renderer-config "$RENDERER_CONFIG" \
    --usd "$usd_path" \
    --output-dir "$OUTPUT_DIR" \
    --width "$WIDTH" \
    --height "$HEIGHT"
}

function batch() {
  local dataset_config="config/datasets/ubuntu.json"
  if (($# > 0)) && [[ "$1" != -* ]]; then
    dataset_config="$1"
    shift
  fi

  python3 -m tools.hydra_batch \
    --dataset-config "$dataset_config" \
    --renderer-config "$RENDERER_CONFIG" \
    --hydra-capture "$HYDRA_CAPTURE" \
    --output-dir "$OUTPUT_DIR/ubuntu" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    "$@"
}

function dataset() {
  batch "${1:-config/datasets/ubuntu.json}" "${@:2}"
}

function baseline() {
  python3 -m tools.hydra_batch.workflow baseline --test-config "$TEST_CONFIG" "$@"
}

function test() {
  python3 -m tools.hydra_batch.workflow test --test-config "$TEST_CONFIG" "$@"
}

if (($# > 0)) && declare -f -- "$1" > /dev/null; then
  "$1" "${@:2}"
else
  batch "$@"
fi
