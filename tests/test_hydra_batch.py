import contextlib
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.hydra_batch import runner


class HydraBatchTests(unittest.TestCase):
    def write_json(self, root: Path, name: str, value: dict) -> Path:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def dataset_json(self, root_dir: Path) -> dict:
        return {
            "root_dir": str(root_dir),
            "relative_path": [
                "pbr/redbox/redbox_right.usd",
                "single/four_box/mesh_output_1761207339.usdc",
            ],
        }

    def renderer_json(self) -> dict:
        return {
            "name": "hdRobot",
            "renderer_plugin": "HdRobotRendererPlugin",
            "defaults": {
                "aovs": ["color", "depth", "lidar:pointCloud"],
                "maxIterations": 4,
            },
            "aovs": {
                "color": {"output_ext": ".png"},
                "lidar:pointCloud": {"output_ext": ".exr"},
            },
        }

    def test_loads_configs_and_expands_dataset_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            asset_root = tmp_path / "assets"
            output_dir = tmp_path / "out"
            dataset_path = self.write_json(
                tmp_path,
                "dataset.json",
                self.dataset_json(asset_root),
            )
            renderer_path = self.write_json(tmp_path, "plugin.json", self.renderer_json())

            dataset = runner.load_dataset_config(dataset_path)
            renderer = runner.load_renderer_config(renderer_path)
            cases = runner.build_case_plans(
                dataset,
                renderer,
                renderer_config_path=renderer_path,
                hydra_capture_path=Path("build-codex/hydra_capture"),
                output_dir=output_dir,
                width=64,
                height=32,
            )

            self.assertEqual(renderer.default_aovs, ["color", "depth", "lidar:pointCloud"])
            self.assertEqual(renderer.max_iterations, 4)
            self.assertEqual(
                [case.usd_path for case in cases],
                [
                    asset_root / "pbr/redbox/redbox_right.usd",
                    asset_root / "single/four_box/mesh_output_1761207339.usdc",
                ],
            )

    def test_expected_outputs_use_hydra_capture_aov_rules(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            dataset = runner.DatasetConfig(
                root_dir=tmp_path / "assets",
                relative_paths=["scene.usd"],
            )
            renderer_config = runner.RendererConfig(
                default_aovs=["color", "depth", "lidar:pointCloud"],
                output_exts={"color": ".png", "lidar:pointCloud": ".exr"},
                max_iterations=None,
            )

            case = runner.build_case_plans(
                dataset,
                renderer_config,
                renderer_config_path=tmp_path / "plugin.json",
                hydra_capture_path=tmp_path / "hydra_capture",
                output_dir=tmp_path / "output",
                width=1280,
                height=720,
            )[0]

            self.assertEqual(
                runner.expected_output_paths(case),
                [
                    tmp_path / "output/scene/color.png",
                    tmp_path / "output/scene/depth.ppm",
                    tmp_path / "output/scene/lidar_pointCloud.exr",
                ],
            )

    def test_builds_hydra_capture_command_with_overrides(self):
        case = runner.CasePlan(
            usd_path=Path("/assets/scene.usd"),
            output_dir=Path("/tmp/output"),
            renderer_config_path=Path("config/plugins/hdStorm/plugin.json"),
            hydra_capture_path=Path("build-codex/hydra_capture"),
            width=320,
            height=180,
            aovs=["color"],
            output_exts={"color": ".png"},
            max_iterations=2,
        )

        self.assertEqual(
            runner.build_command(case),
            [
                "build-codex/hydra_capture",
                "--renderer-config",
                "config/plugins/hdStorm/plugin.json",
                "--usd",
                "/assets/scene.usd",
                "--output-dir",
                "/tmp/output",
                "--width",
                "320",
                "--height",
                "180",
                "--max-iterations",
                "2",
            ],
        )

    def test_dry_run_reports_planned_cases_without_running_subprocess(self):
        case = runner.CasePlan(
            usd_path=Path("/assets/scene.usd"),
            output_dir=Path("/tmp/output"),
            renderer_config_path=Path("plugin.json"),
            hydra_capture_path=Path("hydra_capture"),
            width=64,
            height=32,
            aovs=["color"],
            output_exts={"color": ".png"},
            max_iterations=None,
        )

        def fail_runner(*_args, **_kwargs):
            raise AssertionError("dry-run must not execute subprocesses")

        report = runner.run_case(case, dry_run=True, subprocess_runner=fail_runner)

        self.assertTrue(report.success)
        self.assertTrue(report.dry_run)
        self.assertEqual(report.command[0], "hydra_capture")

    def test_run_case_fails_when_expected_output_is_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            case = runner.CasePlan(
                usd_path=tmp_path / "assets/scene.usd",
                output_dir=tmp_path / "output",
                renderer_config_path=tmp_path / "plugin.json",
                hydra_capture_path=tmp_path / "hydra_capture",
                width=64,
                height=32,
                aovs=["color", "depth"],
                output_exts={"color": ".png"},
                max_iterations=None,
            )

            def fake_runner(*_args, **_kwargs):
                output_file = tmp_path / "output/scene/color.png"
                output_file.parent.mkdir(parents=True, exist_ok=True)
                output_file.write_bytes(b"png")
                return subprocess.CompletedProcess(args=[], returncode=0, stdout="", stderr="")

            report = runner.run_case(case, subprocess_runner=fake_runner)

            self.assertFalse(report.success)
            self.assertEqual(report.missing_outputs, [tmp_path / "output/scene/depth.ppm"])

    def test_run_batch_returns_nonzero_when_any_case_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            dataset_path = self.write_json(
                tmp_path,
                "dataset.json",
                self.dataset_json(tmp_path / "assets"),
            )
            renderer_path = self.write_json(tmp_path, "plugin.json", self.renderer_json())

            def fake_runner(*_args, **_kwargs):
                return subprocess.CompletedProcess(args=[], returncode=1, stdout="out", stderr="err")

            report = runner.run_batch(
                dataset_config_path=dataset_path,
                renderer_config_path=renderer_path,
                hydra_capture_path=tmp_path / "hydra_capture",
                output_dir=tmp_path / "output",
                width=64,
                height=32,
                subprocess_runner=fake_runner,
            )

            self.assertEqual(report.exit_code, 1)
            self.assertEqual(len(report.cases), 2)
            self.assertFalse(report.cases[0].success)

    def test_parse_args_requires_runtime_resolution_and_output_dir(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            runner.parse_args(
                [
                    "--dataset-config",
                    "config/datasets/unit_test.json",
                    "--renderer-config",
                    "config/plugins/hdStorm/plugin.json",
                ]
            )

        args = runner.parse_args(
            [
                "--dataset-config",
                "config/datasets/unit_test.json",
                "--renderer-config",
                "config/plugins/hdStorm/plugin.json",
                "--output-dir",
                "/tmp/hydra-output",
                "--width",
                "320",
                "--height",
                "180",
            ]
        )

        self.assertEqual(args.output_dir, "/tmp/hydra-output")
        self.assertEqual(args.width, 320)
        self.assertEqual(args.height, 180)


if __name__ == "__main__":
    unittest.main()
