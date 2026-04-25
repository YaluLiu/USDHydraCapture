from __future__ import annotations

import argparse
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from tools.hydra_batch import comparison
from tools.hydra_batch import runner
from tools.hydra_batch import test_config


@dataclass(frozen=True)
class TestAndCompareReport:
    render_report: runner.BatchReport
    compare_report: comparison.ComparisonReport
    exit_code: int


def render_baseline(
    test_config_path: str | Path,
    dry_run: bool = False,
    subprocess_runner: runner.SubprocessRunner = subprocess.run,
) -> runner.BatchReport:
    config = test_config.load_test_config(test_config_path)
    return runner.run_batch(
        dataset_config_path=config.dataset_config_path,
        renderer_config_path=config.renderer_config_path,
        hydra_capture_path=config.hydra_capture_path,
        output_dir=config.baseline_dir,
        width=config.width,
        height=config.height,
        max_iterations=config.max_iterations,
        dry_run=dry_run,
        subprocess_runner=subprocess_runner,
    )


def render_test_and_compare(
    test_config_path: str | Path,
    dry_run: bool = False,
    subprocess_runner: runner.SubprocessRunner = subprocess.run,
) -> TestAndCompareReport:
    config = test_config.load_test_config(test_config_path)
    render_report = runner.run_batch(
        dataset_config_path=config.dataset_config_path,
        renderer_config_path=config.renderer_config_path,
        hydra_capture_path=config.hydra_capture_path,
        output_dir=config.output_dir,
        width=config.width,
        height=config.height,
        max_iterations=config.max_iterations,
        dry_run=dry_run,
        subprocess_runner=subprocess_runner,
    )
    if dry_run:
        return TestAndCompareReport(
            render_report=render_report,
            compare_report=comparison.ComparisonReport(results=[], exit_code=0),
            exit_code=render_report.exit_code,
        )
    if render_report.exit_code != 0:
        return TestAndCompareReport(
            render_report=render_report,
            compare_report=comparison.ComparisonReport(results=[], exit_code=0),
            exit_code=render_report.exit_code,
        )

    dataset = runner.load_dataset_config(config.dataset_config_path)
    renderer_config = runner.load_renderer_config(config.renderer_config_path)
    baseline_cases = runner.build_case_plans(
        dataset,
        renderer_config,
        renderer_config_path=config.renderer_config_path,
        hydra_capture_path=config.hydra_capture_path,
        output_dir=config.baseline_dir,
        width=config.width,
        height=config.height,
        max_iterations_override=config.max_iterations,
    )
    test_cases = runner.build_case_plans(
        dataset,
        renderer_config,
        renderer_config_path=config.renderer_config_path,
        hydra_capture_path=config.hydra_capture_path,
        output_dir=config.output_dir,
        width=config.width,
        height=config.height,
        max_iterations_override=config.max_iterations,
    )

    results: list[comparison.ComparisonResult] = []
    for baseline_case, test_case in zip(baseline_cases, test_cases):
        case_report = comparison.compare_case_outputs(
            baseline_case,
            test_case,
            renderer_config_path=config.renderer_config_path,
        )
        results.extend(case_report.results)

    compare_report = comparison.ComparisonReport(
        results=results,
        exit_code=0 if all(result.success for result in results) else 1,
    )
    return TestAndCompareReport(
        render_report=render_report,
        compare_report=compare_report,
        exit_code=compare_report.exit_code,
    )


def _print_batch_report(report: runner.BatchReport) -> None:
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
    print(f"batch exit_code: {report.exit_code}")


def _print_compare_report(report: comparison.ComparisonReport) -> None:
    if not report.results:
        print("comparison: skipped")
        print(f"comparison exit_code: {report.exit_code}")
        return

    for result in report.results:
        state = "SKIP" if result.skipped else ("PASS" if result.success else "FAIL")
        print(f"[{state}] {result.aov}")
        print(f"  baseline: {result.baseline_path}")
        print(f"  test: {result.test_path}")
        print(f"  diff_ratio: {result.diff_ratio:.6f}")
        print(f"  max_diff_ratio: {result.max_diff_ratio:.6f}")
        if result.error:
            print(f"  error: {result.error}")
    print(f"comparison exit_code: {report.exit_code}")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render hydra_capture test baselines and compare test outputs."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("baseline", "test"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument(
            "--test-config",
            required=True,
            help="Path to workflow test config JSON.",
        )
        subparser.add_argument(
            "--dry-run",
            action="store_true",
            help="Print planned render commands without executing hydra_capture.",
        )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "baseline":
            report = render_baseline(args.test_config, dry_run=args.dry_run)
            _print_batch_report(report)
            return report.exit_code

        report = render_test_and_compare(args.test_config, dry_run=args.dry_run)
    except ValueError as exc:
        print(exc)
        return 2

    _print_batch_report(report.render_report)
    _print_compare_report(report.compare_report)
    return report.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
