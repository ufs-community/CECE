#!/usr/bin/env python3
"""Validate CECE YAML configurations and YAML examples in Markdown."""

from __future__ import annotations

import argparse
import importlib.util
import json
import logging
import re
import sys
import textwrap
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

logger = logging.getLogger("validate_cece_config")

REPO_ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = REPO_ROOT / "src" / "python" / "config.py"
SKIP_MARKER = "<!-- cece-validate: skip -->"
OVERVIEW_MARKER = "<!-- cece-validate: overview -->"
CONTEXT_MARKER_RE = re.compile(
    r"<!--\s*cece-validate:\s*context\s+([A-Za-z_][A-Za-z0-9_-]*(?:\.[A-Za-z_][A-Za-z0-9_-]*)*)\s*-->"
)
DEFAULT_PATTERNS = (
    "examples/*.yaml",
    "scripts/examples/*.yaml",
    "tests/*.yaml",
    "tests/data/*.yaml",
    "README.md",
    "docs/**/*.md",
)
FENCE_OPEN_RE = re.compile(r"^(?P<indent>[ \t]*)(?P<fence>`{3,}|~{3,})(?P<info>.*)$")
PATH_TOKEN_RE = re.compile(r"(?:^|\.)([^.\[\]]+)|\[(\d+)\]")
SCHEME_REGISTRATION_RE = re.compile(
    r'\bPhysicsRegistration\s*<[^>]+>\s+\w+\s*\(\s*"([^"]+)"\s*\)'
)
DIRECT_SCHEME_RE = re.compile(r'\bregister_scheme\s*\(\s*"([^"]+)"')


@dataclass
class MarkdownYamlBlock:
    start_line: int
    content: str
    skipped: bool
    context_path: str | None
    overview: bool


def load_config_module() -> Any:
    spec = importlib.util.spec_from_file_location("cece_config_schema", CONFIG_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load config schema from {CONFIG_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def discover_default_files() -> list[Path]:
    files = set()
    for pattern in DEFAULT_PATTERNS:
        files.update(REPO_ROOT.glob(pattern))
    return sorted(path for path in files if path.is_file())


def discover_registered_schemes() -> set[str]:
    schemes = set()
    for source in (REPO_ROOT / "src" / "core" / "physics").glob("*.cpp"):
        text = source.read_text(encoding="utf-8")
        schemes.update(SCHEME_REGISTRATION_RE.findall(text))
        schemes.update(DIRECT_SCHEME_RE.findall(text))
    return schemes


def _is_closing_fence(line: str, fence: str) -> bool:
    marker = re.escape(fence[0])
    return re.fullmatch(rf"[ \t]*{marker}{{{len(fence)},}}[ \t]*", line) is not None


def _skip_marker_is_near(lines: list[str], opening_line_index: int) -> bool:
    previous = opening_line_index - 1
    while previous >= 0 and not lines[previous].strip():
        previous -= 1
    return previous >= 0 and lines[previous].strip() == SKIP_MARKER


def _context_path_is_near(lines: list[str], opening_line_index: int) -> str | None:
    previous = opening_line_index - 1
    while previous >= 0 and not lines[previous].strip():
        previous -= 1
    if previous < 0:
        return None
    match = CONTEXT_MARKER_RE.fullmatch(lines[previous].strip())
    return match.group(1) if match else None


def _overview_marker_is_near(lines: list[str], opening_line_index: int) -> bool:
    previous = opening_line_index - 1
    while previous >= 0 and not lines[previous].strip():
        previous -= 1
    return previous >= 0 and lines[previous].strip() == OVERVIEW_MARKER


def extract_markdown_yaml_blocks(markdown: str) -> Iterable[MarkdownYamlBlock]:
    lines = markdown.splitlines()
    index = 0
    while index < len(lines):
        match = FENCE_OPEN_RE.match(lines[index])
        if match is None:
            index += 1
            continue

        fence = match.group("fence")
        info = match.group("info").strip().split(maxsplit=1)
        is_yaml = bool(info) and info[0].lower() in {"yaml", "yml"}
        body = []
        closing_index = len(lines)
        cursor = index + 1
        while cursor < len(lines):
            if _is_closing_fence(lines[cursor], fence):
                closing_index = cursor
                break
            body.append(lines[cursor])
            cursor += 1

        if is_yaml:
            indent = match.group("indent")
            dedented = [
                line[len(indent) :] if indent and line.startswith(indent) else line
                for line in body
            ]
            yield MarkdownYamlBlock(
                start_line=index + 1,
                content=textwrap.dedent("\n".join(dedented)),
                skipped=_skip_marker_is_near(lines, index),
                context_path=_context_path_is_near(lines, index),
                overview=_overview_marker_is_near(lines, index),
            )

        index = closing_index + 1 if closing_index < len(lines) else len(lines)


def _yaml_line_for_path(yaml_text: str, path: str) -> int:
    import yaml

    try:
        node = yaml.compose(yaml_text, Loader=yaml.SafeLoader)
    except yaml.YAMLError:
        return 1
    if node is None:
        return 1

    for token_match in PATH_TOKEN_RE.finditer(path):
        key, sequence_index = token_match.groups()
        if key is not None:
            if key == "configuration":
                continue
            if not isinstance(node, yaml.MappingNode):
                break
            match = next(
                (
                    (key_node, value_node)
                    for key_node, value_node in node.value
                    if isinstance(key_node, yaml.ScalarNode) and key_node.value == key
                ),
                None,
            )
            if match is None:
                return node.start_mark.line + 1
            node = match[1]
        else:
            if not isinstance(node, yaml.SequenceNode):
                break
            item_index = int(sequence_index)
            if item_index >= len(node.value):
                return node.start_mark.line + 1
            node = node.value[item_index]
    return node.start_mark.line + 1


def _species_boolean_key_sources(yaml_text: str) -> dict[bool, tuple[str, int]]:
    import yaml

    try:
        root = yaml.compose(yaml_text, Loader=yaml.SafeLoader)
    except yaml.YAMLError:
        return {}
    if not isinstance(root, yaml.MappingNode):
        return {}

    species_node = next(
        (
            value_node
            for key_node, value_node in root.value
            if isinstance(key_node, yaml.ScalarNode) and key_node.value == "species"
        ),
        None,
    )
    if not isinstance(species_node, yaml.MappingNode):
        return {}

    bool_values = {
        "yes": True,
        "no": False,
        "on": True,
        "off": False,
        "true": True,
        "false": False,
    }
    ambiguous_keys = {}
    for key_node, _ in species_node.value:
        if (
            isinstance(key_node, yaml.ScalarNode)
            and key_node.tag == "tag:yaml.org,2002:bool"
            and key_node.value.lower() in bool_values
        ):
            bool_value = bool_values[key_node.value.lower()]
            ambiguous_keys[bool_value] = (
                key_node.value,
                key_node.start_mark.line + 1,
            )
    return ambiguous_keys


def _schema_error_path(error_line: str) -> str | None:
    match = re.match(r"\s*-\s*([^:]+):\s*(.*)$", error_line)
    if match is None:
        return None
    path, message = match.groups()
    unknown_key = re.search(r"unknown key '([^']+)'", message)
    if unknown_key:
        path = f"{path}.{unknown_key.group(1)}"
    return path


def _format_schema_error(error_line: str) -> str:
    message = error_line.strip().removeprefix("- ")
    match = re.match(r"([^:]+):\s*(.*)$", message)
    if match is None:
        return message
    path, detail = match.groups()
    repeated_prefix = f"{path}: "
    detail = detail.removeprefix(repeated_prefix)
    return f"{path}: {detail}"


def _is_speciation_yaml(data: dict) -> bool:
    keys = set(data)
    return {"mechanism", "datasets"}.issubset(keys) or (
        "name" in data and isinstance(data.get("species"), list)
    )


def _normalize_overview_empty_sections(data: dict) -> None:
    empty_sections = {
        "driver": {"grid": {}},
        "meteorology": {},
        "scale_factors": {},
        "masks": {},
        "temporal_profiles": {},
        "local_time": {},
        "species": {},
        "physics_schemes": [],
        "diagnostics": {},
        "cece_data": {"streams": []},
        "output": {},
        "nuopc": {},
    }
    for key, empty_value in empty_sections.items():
        if key in data and data[key] is None:
            data[key] = empty_value

    driver = data.get("driver")
    if isinstance(driver, dict) and "grid" in driver and driver["grid"] is None:
        driver["grid"] = {}

    cece_data = data.get("cece_data")
    if (
        isinstance(cece_data, dict)
        and "streams" in cece_data
        and cece_data["streams"] is None
    ):
        cece_data["streams"] = []


def _wrap_yaml_fragment(yaml_text: str, context_path: str) -> tuple[str, int]:
    import yaml

    try:
        node = yaml.compose(yaml_text, Loader=yaml.SafeLoader)
    except yaml.YAMLError:
        node = None
    for part in context_path.split("."):
        if not isinstance(node, yaml.MappingNode):
            break
        match = next(
            (
                value_node
                for key_node, value_node in node.value
                if isinstance(key_node, yaml.ScalarNode) and key_node.value == part
            ),
            None,
        )
        if match is None:
            break
        node = match
    else:
        return yaml_text, 0

    parts = context_path.split(".")
    prefix = [f"{'  ' * depth}{json.dumps(part)}:" for depth, part in enumerate(parts)]
    indent = "  " * len(parts)
    fragment = "\n".join(
        f"{indent}{line}" if line.strip() else line for line in yaml_text.splitlines()
    )
    return "\n".join([*prefix, fragment]), len(prefix)


def validate_yaml_text(  # noqa: C901
    yaml_text: str,
    source: str,
    line_offset: int,
    config_module: Any,
    registered_schemes: set[str],
    context_path: str | None = None,
    overview: bool = False,
) -> tuple[list[tuple[int, str]], str | None]:
    import yaml

    context_prefix_lines = 0
    if context_path:
        yaml_text, context_prefix_lines = _wrap_yaml_fragment(yaml_text, context_path)

    def source_line(line: int) -> int:
        return line_offset + max(1, line - context_prefix_lines)

    try:
        data = yaml.safe_load(yaml_text)
    except yaml.YAMLError as error:
        mark = getattr(error, "problem_mark", None)
        line = mark.line + 1 if mark is not None else 1
        return [(source_line(line), str(error).splitlines()[0])], None

    if overview and isinstance(data, dict):
        _normalize_overview_empty_sections(data)

    if isinstance(data, dict) and _is_speciation_yaml(data):
        return [], "speciation YAML"
    if isinstance(data, list):
        return [
            (
                source_line(1),
                (
                    "expected a CECE configuration mapping, not a YAML list; "
                    "put this fragment under its full configuration path or mark it "
                    "with the CECE validation skip comment"
                ),
            )
        ], None
    if not isinstance(data, dict):
        return [(source_line(1), "expected a CECE configuration mapping")], None

    ambiguous_species_keys = {}
    species = data.get("species")
    if isinstance(species, dict) and any(type(name) is bool for name in species):
        ambiguous_species_keys = _species_boolean_key_sources(yaml_text)

    try:
        config_module.CeceConfig.from_dict(data)
    except (TypeError, ValueError) as error:
        errors = []
        for message in str(error).splitlines():
            if message.strip() == "Invalid configuration:":
                continue
            path = _schema_error_path(message)
            line = _yaml_line_for_path(yaml_text, path) if path else 1
            formatted_message = _format_schema_error(message)
            species_match = re.match(r"^species\.(True|False)$", path or "")
            if (
                species_match
                and "species name must be a non-empty string" in message
                and (
                    source_key := ambiguous_species_keys.get(species_match[1] == "True")
                )
            ):
                token, line = source_key
                bool_value = species_match[1].lower()
                formatted_message = (
                    f"species.{token}: PyYAML interpreted unquoted species key "
                    f'{token!r} as boolean {bool_value}; quote it as "{token}" '
                    "to keep it a string"
                )
            errors.append((source_line(line), formatted_message))
        return errors or [(source_line(1), str(error))], None

    errors = []
    if context_path is None:
        for index, scheme in enumerate(data.get("physics_schemes", [])):
            name = scheme.get("name")
            if name not in registered_schemes:
                line = _yaml_line_for_path(yaml_text, f"physics_schemes[{index}].name")
                errors.append((
                    source_line(line),
                    f"unregistered physics scheme {name!r}",
                ))
    return errors, None


def validate_file(
    path: Path,
    display_path: str,
    config_module: Any,
    registered_schemes: set[str],
) -> list[tuple[str, int, str]]:
    try:
        content = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        return [(display_path, 1, str(error))]

    errors = []
    if path.suffix.lower() in {".md", ".markdown"}:
        for block in extract_markdown_yaml_blocks(content):
            if block.skipped:
                logger.info("%s:%d: skipped by marker", display_path, block.start_line)
                continue
            block_errors, skipped_reason = validate_yaml_text(
                block.content,
                display_path,
                block.start_line,
                config_module,
                registered_schemes,
                context_path=block.context_path,
                overview=block.overview,
            )
            if skipped_reason:
                logger.info(
                    "%s:%d: skipped %s", display_path, block.start_line, skipped_reason
                )
            errors.extend(
                (display_path, line, message) for line, message in block_errors
            )
            if not skipped_reason and not block_errors:
                logger.info("%s:%d: valid YAML block", display_path, block.start_line)
    else:
        file_errors, skipped_reason = validate_yaml_text(
            content, display_path, 0, config_module, registered_schemes
        )
        if skipped_reason:
            logger.info("%s: skipped %s", display_path, skipped_reason)
        errors.extend((display_path, line, message) for line, message in file_errors)
        if not skipped_reason and not file_errors:
            logger.info("%s: valid CECE configuration", display_path)
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", help="YAML or Markdown files to validate")
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="report skipped and valid inputs"
    )
    args = parser.parse_args(argv)

    # Bound to the current stderr per call; messages stay "path:line: message" for editors.
    handler = logging.StreamHandler(sys.stderr)
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.handlers[:] = [handler]
    logger.propagate = False
    logger.setLevel(logging.INFO if args.verbose else logging.WARNING)

    if args.paths:
        inputs = [(Path(value), value) for value in args.paths]
    else:
        inputs = [
            (path, path.relative_to(REPO_ROOT).as_posix())
            for path in discover_default_files()
        ]
    if not inputs:
        logger.error("No default CECE YAML or Markdown files were found")
        return 1

    config_module = load_config_module()
    registered_schemes = discover_registered_schemes()
    all_errors = []
    for path, display_path in inputs:
        if not path.is_file():
            all_errors.append((display_path, 1, "file does not exist"))
            continue
        all_errors.extend(
            validate_file(path, display_path, config_module, registered_schemes)
        )

    for path, line, message in all_errors:
        logger.error("%s:%d: %s", path, line, message)
    return 1 if all_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
