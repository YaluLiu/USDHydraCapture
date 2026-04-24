#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p resources

function single() {
  ./build-codex/hydra_capture \
    --renderer-config config/plugins/hdRobot/plugin.json \
    --usd /home/yalu/docker/assets/unit_test/single/four_box/mesh_output_1761207339.usdc \
    --output-dir output \
    --width 1280 \
    --height 720
}

function dataset() {
  python3 -m tools.hydra_batch \
      --dataset-config config/datasets/unit_test.json \
      --renderer-config config/plugins/hdStorm/plugin.json \
      --output-dir output/unit_test \
      --width 1280 \
      --height 720
}
