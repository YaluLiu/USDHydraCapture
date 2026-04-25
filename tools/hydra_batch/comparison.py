from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from tools.hydra_batch import runner


@dataclass(frozen=True)
class ComparisonResult:
    aov: str
    baseline_path: Path
    test_path: Path
    success: bool
    skipped: bool
    diff_ratio: float
    max_diff_ratio: float
    error: str | None = None


@dataclass(frozen=True)
class ComparisonReport:
    results: list[ComparisonResult]
    exit_code: int


@dataclass(frozen=True)
class _CompareRule:
    enabled: bool
    max_diff_ratio: float


def _load_json_object(path: Path) -> dict:
    try:
        with path.open(encoding="utf-8") as stream:
            value = json.load(stream)
    except OSError as exc:
        raise ValueError(f"{path}: failed to open renderer config: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: failed to parse JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{path}: renderer config must be a JSON object")
    return value


def _default_rule_for_aov(aov: str) -> _CompareRule:
    if "pointcloud" in aov.replace("_", "").replace(":", "").lower():
        return _CompareRule(enabled=False, max_diff_ratio=0.0)
    return _CompareRule(enabled=True, max_diff_ratio=0.0)


def _parse_compare_rule(aov: str, aov_config: object, config_path: Path) -> _CompareRule:
    default_rule = _default_rule_for_aov(aov)
    if not isinstance(aov_config, dict):
        return default_rule

    compare = aov_config.get("compare")
    if compare is None:
        return default_rule
    if not isinstance(compare, dict):
        raise ValueError(f"{config_path}: key 'aovs.{aov}.compare' must be an object")

    enabled = compare.get("enabled", default_rule.enabled)
    if not isinstance(enabled, bool):
        raise ValueError(f"{config_path}: key 'aovs.{aov}.compare.enabled' must be a boolean")

    max_diff_ratio = compare.get("max_diff_ratio", default_rule.max_diff_ratio)
    if (
        not isinstance(max_diff_ratio, (int, float))
        or isinstance(max_diff_ratio, bool)
        or max_diff_ratio < 0.0
    ):
        raise ValueError(
            f"{config_path}: key 'aovs.{aov}.compare.max_diff_ratio' must be a non-negative number"
        )

    return _CompareRule(enabled=enabled, max_diff_ratio=float(max_diff_ratio))


def _compare_rules(path: Path, aovs: list[str]) -> dict[str, _CompareRule]:
    root = _load_json_object(path)
    configured_aovs = root.get("aovs", {})
    if not isinstance(configured_aovs, dict):
        raise ValueError(f"{path}: key 'aovs' must be an object")
    return {
        aov: _parse_compare_rule(aov, configured_aovs.get(aov), path)
        for aov in aovs
    }


def _byte_diff_ratio(baseline: bytes, test: bytes) -> float:
    max_length = max(len(baseline), len(test))
    if max_length == 0:
        return 0.0

    differing_bytes = sum(
        1 for baseline_byte, test_byte in zip(baseline, test) if baseline_byte != test_byte
    )
    differing_bytes += abs(len(baseline) - len(test))
    return differing_bytes / max_length


def _compare_output(
    *,
    aov: str,
    baseline_path: Path,
    test_path: Path,
    rule: _CompareRule,
) -> ComparisonResult:
    if not rule.enabled:
        return ComparisonResult(
            aov=aov,
            baseline_path=baseline_path,
            test_path=test_path,
            success=True,
            skipped=True,
            diff_ratio=0.0,
            max_diff_ratio=rule.max_diff_ratio,
        )

    if not baseline_path.is_file():
        return ComparisonResult(
            aov=aov,
            baseline_path=baseline_path,
            test_path=test_path,
            success=False,
            skipped=False,
            diff_ratio=1.0,
            max_diff_ratio=rule.max_diff_ratio,
            error=f"missing baseline output: {baseline_path}",
        )
    if not test_path.is_file():
        return ComparisonResult(
            aov=aov,
            baseline_path=baseline_path,
            test_path=test_path,
            success=False,
            skipped=False,
            diff_ratio=1.0,
            max_diff_ratio=rule.max_diff_ratio,
            error=f"missing test output: {test_path}",
        )

    diff_ratio = _byte_diff_ratio(baseline_path.read_bytes(), test_path.read_bytes())
    return ComparisonResult(
        aov=aov,
        baseline_path=baseline_path,
        test_path=test_path,
        success=diff_ratio <= rule.max_diff_ratio,
        skipped=False,
        diff_ratio=diff_ratio,
        max_diff_ratio=rule.max_diff_ratio,
    )


def compare_case_outputs(
    baseline_case: runner.CasePlan,
    test_case: runner.CasePlan,
    *,
    renderer_config_path: str | Path,
) -> ComparisonReport:
    aovs = list(baseline_case.aovs)
    baseline_paths = runner.expected_output_paths(baseline_case)
    test_paths = runner.expected_output_paths(test_case)
    rules = _compare_rules(Path(renderer_config_path), aovs)

    results = [
        _compare_output(
            aov=aov,
            baseline_path=baseline_path,
            test_path=test_path,
            rule=rules[aov],
        )
        for aov, baseline_path, test_path in zip(aovs, baseline_paths, test_paths)
    ]
    return ComparisonReport(
        results=results,
        exit_code=0 if all(result.success for result in results) else 1,
    )
