from __future__ import annotations

import argparse
import json
import shlex
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Sequence


SubprocessRunner = Callable[..., subprocess.CompletedProcess]


def _positive_int_arg(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a positive integer") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


@dataclass(frozen=True)
class DatasetConfig:
    root_dir: Path
    relative_paths: list[str]


@dataclass(frozen=True)
class RendererConfig:
    default_aovs: list[str]
    output_exts: dict[str, str]
    max_iterations: int | None = None


@dataclass(frozen=True)
class CasePlan:
    usd_path: Path
    output_dir: Path
    renderer_config_path: Path
    hydra_capture_path: Path
    width: int
    height: int
    aovs: list[str]
    output_exts: dict[str, str]
    max_iterations: int | None


@dataclass(frozen=True)
class CaseReport:
    case: CasePlan
    command: list[str]
    success: bool
    dry_run: bool = False
    returncode: int | None = None
    stdout: str = ""
    stderr: str = ""
    missing_outputs: list[Path] = field(default_factory=list)
    error: str | None = None


@dataclass(frozen=True)
class BatchReport:
    cases: list[CaseReport]
    exit_code: int


def _load_json_object(path: Path, label: str) -> dict:
    try:
        with path.open(encoding="utf-8") as stream:
            value = json.load(stream)
    except OSError as exc:
        raise ValueError(f"{path}: failed to open {label}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: failed to parse JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{path}: {label} must be a JSON object")
    return value


def _string_member(source: dict, key: str, path: Path) -> str:
    value = source.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{path}: key '{key}' must be a non-empty string")
    return value


def _optional_positive_int(source: dict, key: str, path: Path) -> int | None:
    if key not in source:
        return None
    value = source[key]
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise ValueError(f"{path}: key '{key}' must be a positive integer")
    return value


def _string_array_member(source: dict, key: str, path: Path) -> list[str]:
    value = source.get(key)
    if not isinstance(value, list):
        raise ValueError(f"{path}: key '{key}' must be an array of strings")
    result = []
    for index, item in enumerate(value):
        if not isinstance(item, str) or not item:
            raise ValueError(f"{path}: key '{key}[{index}]' must be a non-empty string")
        result.append(item)
    return result


def load_dataset_config(path: str | Path) -> DatasetConfig:
    config_path = Path(path)
    root = _load_json_object(config_path, "dataset config")
    files = root.get("files", root)
    if not isinstance(files, dict):
        raise ValueError(f"{config_path}: key 'files' must be an object")

    return DatasetConfig(
        root_dir=Path(_string_member(files, "root_dir", config_path)),
        relative_paths=_string_array_member(files, "relative_path", config_path),
    )


def load_renderer_config(path: str | Path) -> RendererConfig:
    config_path = Path(path)
    root = _load_json_object(config_path, "renderer config")
    defaults = root.get("defaults", {})
    if not isinstance(defaults, dict):
        raise ValueError(f"{config_path}: key 'defaults' must be an object")

    default_aovs = defaults.get("aovs", [])
    if default_aovs == []:
        parsed_aovs: list[str] = []
    elif isinstance(default_aovs, list):
        parsed_aovs = []
        for index, item in enumerate(default_aovs):
            if not isinstance(item, str) or not item:
                raise ValueError(
                    f"{config_path}: key 'defaults.aovs[{index}]' must be a non-empty string"
                )
            parsed_aovs.append(item)
    else:
        raise ValueError(f"{config_path}: key 'defaults.aovs' must be an array of strings")

    aovs = root.get("aovs", {})
    if not isinstance(aovs, dict):
        raise ValueError(f"{config_path}: key 'aovs' must be an object")
    output_exts: dict[str, str] = {}
    for aov, value in aovs.items():
        if not isinstance(aov, str) or not aov:
            raise ValueError(f"{config_path}: AOV keys must be non-empty strings")
        if not isinstance(value, dict):
            raise ValueError(f"{config_path}: key 'aovs.{aov}' must be an object")
        output_ext = value.get("output_ext")
        if output_ext is None:
            continue
        if not isinstance(output_ext, str) or not output_ext:
            raise ValueError(f"{config_path}: key 'aovs.{aov}.output_ext' must be a string")
        output_exts[aov] = output_ext

    return RendererConfig(
        default_aovs=parsed_aovs,
        output_exts=output_exts,
        max_iterations=_optional_positive_int(defaults, "maxIterations", config_path),
    )


def sanitize_file_name_token(token: str) -> str:
    output = []
    previous_was_separator = False
    for char in token:
        if char.isascii() and (char.isalnum() or char in "_-."):
            output.append(char)
            previous_was_separator = False
        elif not previous_was_separator:
            output.append("_")
            previous_was_separator = True
    return "".join(output)


def resolve_aovs(renderer_config: RendererConfig) -> list[str]:
    return renderer_config.default_aovs or ["color"]


def resolve_output_ext(aov: str, output_exts: dict[str, str]) -> str:
    if aov in output_exts:
        return output_exts[aov]
    if aov == "color":
        return ".png"
    return ".ppm"


def build_case_plans(
    dataset: DatasetConfig,
    renderer_config: RendererConfig,
    *,
    renderer_config_path: str | Path,
    hydra_capture_path: str | Path,
    output_dir: str | Path,
    width: int,
    height: int,
    max_iterations_override: int | None = None,
) -> list[CasePlan]:
    max_iterations = (
        max_iterations_override
        if max_iterations_override is not None
        else renderer_config.max_iterations
    )
    aovs = resolve_aovs(renderer_config)
    return [
        CasePlan(
            usd_path=dataset.root_dir / relative_path,
            output_dir=Path(output_dir),
            renderer_config_path=Path(renderer_config_path),
            hydra_capture_path=Path(hydra_capture_path),
            width=width,
            height=height,
            aovs=list(aovs),
            output_exts=dict(renderer_config.output_exts),
            max_iterations=max_iterations,
        )
        for relative_path in dataset.relative_paths
    ]


def expected_output_paths(case: CasePlan) -> list[Path]:
    return [
        case.output_dir
        / case.usd_path.stem
        / f"{sanitize_file_name_token(aov)}{resolve_output_ext(aov, case.output_exts)}"
        for aov in case.aovs
    ]


def build_command(case: CasePlan) -> list[str]:
    command = [
        str(case.hydra_capture_path),
        "--renderer-config",
        str(case.renderer_config_path),
        "--usd",
        str(case.usd_path),
        "--output-dir",
        str(case.output_dir),
        "--width",
        str(case.width),
        "--height",
        str(case.height),
    ]
    if case.max_iterations is not None:
        command.extend(["--max-iterations", str(case.max_iterations)])
    return command


def run_case(
    case: CasePlan,
    *,
    dry_run: bool = False,
    subprocess_runner: SubprocessRunner = subprocess.run,
) -> CaseReport:
    command = build_command(case)
    if dry_run:
        return CaseReport(case=case, command=command, success=True, dry_run=True)

    try:
        completed = subprocess_runner(command, capture_output=True, text=True)
    except OSError as exc:
        return CaseReport(case=case, command=command, success=False, error=str(exc))

    if completed.returncode != 0:
        return CaseReport(
            case=case,
            command=command,
            success=False,
            returncode=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
        )

    missing_outputs = [path for path in expected_output_paths(case) if not path.is_file()]
    return CaseReport(
        case=case,
        command=command,
        success=not missing_outputs,
        returncode=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
        missing_outputs=missing_outputs,
    )


def run_batch(
    *,
    dataset_config_path: str | Path,
    renderer_config_path: str | Path,
    output_dir: str | Path,
    width: int,
    height: int,
    hydra_capture_path: str | Path = Path("build-codex/hydra_capture"),
    max_iterations: int | None = None,
    dry_run: bool = False,
    subprocess_runner: SubprocessRunner = subprocess.run,
) -> BatchReport:
    dataset = load_dataset_config(dataset_config_path)
    renderer_config = load_renderer_config(renderer_config_path)
    cases = build_case_plans(
        dataset,
        renderer_config,
        renderer_config_path=renderer_config_path,
        hydra_capture_path=hydra_capture_path,
        output_dir=output_dir,
        width=width,
        height=height,
        max_iterations_override=max_iterations,
    )
    reports = [
        run_case(case, dry_run=dry_run, subprocess_runner=subprocess_runner)
        for case in cases
    ]
    return BatchReport(
        cases=reports,
        exit_code=0 if all(report.success for report in reports) else 1,
    )


def _print_report(report: BatchReport) -> None:
    for case_report in report.cases:
        state = "DRY-RUN" if case_report.dry_run else ("PASS" if case_report.success else "FAIL")
        print(f"[{state}] {case_report.case.usd_path}")
        print("  command: " + shlex.join(case_report.command))
        if case_report.returncode not in (None, 0):
            print(f"  returncode: {case_report.returncode}")
        if case_report.error:
            print(f"  error: {case_report.error}")
        if case_report.missing_outputs:
            print("  missing outputs:")
            for path in case_report.missing_outputs:
                print(f"    {path}")
        if case_report.stdout:
            print("  stdout:")
            print(case_report.stdout.rstrip())
        if case_report.stderr:
            print("  stderr:")
            print(case_report.stderr.rstrip())


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Batch-run hydra_capture over a dataset JSON."
    )
    parser.add_argument("--dataset-config", required=True, help="Path to dataset JSON.")
    parser.add_argument("--renderer-config", required=True, help="Path to renderer plugin JSON.")
    parser.add_argument(
        "--hydra-capture",
        default="build-codex/hydra_capture",
        help="Path to hydra_capture executable.",
    )
    parser.add_argument("--output-dir", required=True, help="Root directory for rendered outputs.")
    parser.add_argument(
        "--width",
        required=True,
        type=_positive_int_arg,
        help="Input render width in pixels.",
    )
    parser.add_argument(
        "--height",
        required=True,
        type=_positive_int_arg,
        help="Input render height in pixels.",
    )
    parser.add_argument(
        "--max-iterations",
        type=int,
        help="Override plugin defaults.maxIterations.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned commands without executing them.",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    if args.max_iterations is not None and args.max_iterations <= 0:
        raise SystemExit("--max-iterations must be a positive integer")
    try:
        report = run_batch(
            dataset_config_path=args.dataset_config,
            renderer_config_path=args.renderer_config,
            hydra_capture_path=args.hydra_capture,
            output_dir=args.output_dir,
            width=args.width,
            height=args.height,
            max_iterations=args.max_iterations,
            dry_run=args.dry_run,
        )
    except ValueError as exc:
        print(exc)
        return 2
    _print_report(report)
    return report.exit_code
