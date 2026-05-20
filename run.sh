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

mkdir -p "$OUTPUT_DIR"

function aovs() {
  local usd_path="${1:-$DEFAULT_USD}"
  if (($# > 0)); then
    shift
  fi

  local renderer_config="$RENDERER_CONFIG"
  if (($# > 0)) && [[ "$1" != -* ]]; then
    if [[ "$1" == *.json || "$1" == */* ]]; then
      renderer_config="$1"
    else
      renderer_config="config/plugins/${1}/plugin.json"
    fi
    shift
  fi

  local plugin_name
  plugin_name="$(basename "$(dirname "$renderer_config")")"
  local output_dir="$OUTPUT_DIR/aovs/${plugin_name}"
  if (($# > 0)) && [[ "$1" != -* ]]; then
    output_dir="$1"
    shift
  fi

  local pxr_pluginpath="${PXR_PLUGINPATH_NAME:-/home/yalu/software/USD/plugin/usd}"
  PXR_PLUGINPATH_NAME="$pxr_pluginpath" "$HYDRA_CAPTURE" \
    --renderer-config "$renderer_config" \
    --usd "$usd_path" \
    --output-dir "$output_dir" \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    "$@"
}

function build() {
  cmake --build build-codex --target hydra_capture
}

if (($# > 0)) && declare -f -- "$1" > /dev/null; then
  "$1" "${@:2}"
else
  aovs "$@"
fi
