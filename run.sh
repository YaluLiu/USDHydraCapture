#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p resources

PLUGIN_NAME="hdRobot"
HYDRA_CAPTURE="./build-codex/hydra_capture"
RENDERER_CONFIG="config/plugins/${PLUGIN_NAME}/plugin.json"
TEST_CONFIG="config/test/ubuntu.json"
DEFAULT_USD="/home/yalu/docker/assets/unit_test/single/four_box/mesh_output_1761207339.usdc"

function single() {
  local usd_path="${1:-$DEFAULT_USD}"

  "$HYDRA_CAPTURE" \
    --renderer-config "$RENDERER_CONFIG" \
    --usd "$usd_path" \
    --output-dir output \
    --width 3840 \
    --height 2160
}

function hydra() {
  local dataset_config="config/datasets/ubuntu.json"
  if (($# > 0)) && [[ "$1" != -* ]]; then
    dataset_config="$1"
    shift
  fi

  python3 -m tools.hydra_batch \
    --dataset-config "$dataset_config" \
    --renderer-config "$RENDERER_CONFIG" \
    --hydra-capture "$HYDRA_CAPTURE" \
    --output-dir output/ubuntu \
    --width 3840 \
    --height 2160 \
    "$@"
}

function dataset() {
  hydra "${1:-config/datasets/ubuntu.json}" "${@:2}"
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
  hydra "$@"
fi
