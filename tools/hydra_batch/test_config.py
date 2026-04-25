from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class TestConfig:
    renderer_config_path: Path
    dataset_config_path: Path
    hydra_capture_path: Path
    baseline_dir: Path
    output_dir: Path
    width: int
    height: int
    max_iterations: int | None = None


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


def _reject_unexpected_keys(
    source: dict, allowed_keys: set[str], path: Path, prefix: str | None = None
) -> None:
    for key in source:
        if key not in allowed_keys:
            display_key = f"{prefix}.{key}" if prefix else key
            raise ValueError(f"{path}: unexpected key '{display_key}'")


def _string_member(source: dict, key: str, path: Path) -> str:
    value = source.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{path}: key '{key}' must be a non-empty string")
    return value


def _object_member(source: dict, key: str, path: Path) -> dict:
    value = source.get(key)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: key '{key}' must be an object")
    return value


def _positive_int_member(
    source: dict, key: str, path: Path, display_key: str | None = None
) -> int:
    value = source.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise ValueError(f"{path}: key '{display_key or key}' must be a positive integer")
    return value


def _optional_positive_int(source: dict, key: str, path: Path) -> int | None:
    if key not in source:
        return None
    value = source[key]
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise ValueError(f"{path}: key '{key}' must be a positive integer")
    return value


def load_test_config(path: str | Path) -> TestConfig:
    config_path = Path(path)
    root = _load_json_object(config_path, "test config")
    _reject_unexpected_keys(
        root,
        {
            "renderer_config",
            "dataset_config",
            "hydra_capture",
            "resolution",
            "baseline_dir",
            "output_dir",
            "max_iterations",
        },
        config_path,
    )

    resolution = _object_member(root, "resolution", config_path)
    _reject_unexpected_keys(
        resolution, {"width", "height"}, config_path, prefix="resolution"
    )

    return TestConfig(
        renderer_config_path=Path(_string_member(root, "renderer_config", config_path)),
        dataset_config_path=Path(_string_member(root, "dataset_config", config_path)),
        hydra_capture_path=Path(_string_member(root, "hydra_capture", config_path)),
        baseline_dir=Path(_string_member(root, "baseline_dir", config_path)),
        output_dir=Path(_string_member(root, "output_dir", config_path)),
        width=_positive_int_member(
            resolution, "width", config_path, display_key="resolution.width"
        ),
        height=_positive_int_member(
            resolution, "height", config_path, display_key="resolution.height"
        ),
        max_iterations=_optional_positive_int(root, "max_iterations", config_path),
    )
