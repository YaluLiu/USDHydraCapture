# Usage:
#   .\render_pao_hdrobot.ps1 single D:\assets\scene.usd
#   .\render_pao_hdrobot.ps1 dataset config\datasets\windows.json
#   .\render_pao_hdrobot.ps1 baseline
#   .\render_pao_hdrobot.ps1 test

Set-Location $PSScriptRoot

$mode = $args[0]
$inputPath = $args[1]
$hydra = ".\build-codex\Release\hydra_capture.exe"
$pluginName="hdRobot"  # hdRobot hdStorm

if ($mode -eq "single") {
  & $hydra `
    --renderer-config config\plugins\$pluginName\plugin.json `
    --usd $inputPath `
    --output-dir output `
    --width 3840 `
    --height 2160
} elseif ($mode -eq "baseline") {
  python -m tools.hydra_batch.workflow baseline --test-config config\test\windows.json
} elseif ($mode -eq "test") {
  python -m tools.hydra_batch.workflow test --test-config config\test\windows.json
} else {
  python -m tools.hydra_batch `
    --dataset-config $inputPath `
    --renderer-config config\plugins\$pluginName\plugin.json `
    --hydra-capture $hydra `
    --output-dir output\windows `
    --width 3840 `
    --height 2160
}
