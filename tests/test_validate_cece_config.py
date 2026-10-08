import importlib.util
import sys
from pathlib import Path

import pytest

SCRIPT_PATH = Path(__file__).parents[1] / "scripts" / "validate_cece_config.py"
SPEC = importlib.util.spec_from_file_location("cece_config_validator", SCRIPT_PATH)
validator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = validator
SPEC.loader.exec_module(validator)


def test_indented_yaml_fence_is_dedented_and_keeps_its_source_line(tmp_path, capsys):
    markdown = tmp_path / "guide.md"
    markdown.write_text(
        "!!! note\n    ```yaml\n    species: {}\n    ```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown)])

    assert result == 0
    assert capsys.readouterr().err == ""


def test_skip_marker_skips_the_nearest_yaml_fence(tmp_path, capsys):
    markdown = tmp_path / "guide.md"
    markdown.write_text(
        "<!-- cece-validate: skip -->\n\n```yaml\n- layer fragment\n```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown), "--verbose"])

    assert result == 0
    assert "skipped by marker" in capsys.readouterr().err


def test_context_directive_validates_fragment_under_declared_path(tmp_path, capsys):
    markdown = tmp_path / "layers.md"
    markdown.write_text(
        "<!-- cece-validate: context species.NO -->\n"
        "```yaml\n"
        "- field: NO_EMIS\n"
        "  operation: add\n"
        "```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown)])

    assert result == 0
    assert capsys.readouterr().err == ""


def test_contextual_scheme_fragment_checks_schema_without_registry_lookup(
    tmp_path, capsys
):
    markdown = tmp_path / "scheme_fragment.md"
    markdown.write_text(
        "<!-- cece-validate: context physics_schemes -->\n"
        "```yaml\n"
        "- name: my_unregistered_scheme\n"
        "  options:\n"
        "    coefficient: 1.0\n"
        "```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown)])

    assert result == 0
    assert capsys.readouterr().err == ""


def test_contextual_scheme_section_can_include_its_context_key(tmp_path, capsys):
    markdown = tmp_path / "scheme_section.md"
    markdown.write_text(
        "<!-- cece-validate: context physics_schemes -->\n"
        "```yaml\n"
        "physics_schemes:\n"
        "  - name: my_unregistered_scheme\n"
        "    options: {coefficient: 1.0}\n"
        "```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown)])

    assert result == 0
    assert capsys.readouterr().err == ""


def test_overview_directive_treats_comment_only_sections_as_empty(tmp_path, capsys):
    markdown = tmp_path / "overview.md"
    markdown.write_text(
        "<!-- cece-validate: overview -->\n"
        "```yaml\n"
        "driver:\n"
        "  # Timing and grid settings go here.\n"
        "  grid:\n"
        "    # Grid dimensions go here.\n"
        "meteorology:\n"
        "  # Map meteorological field names here.\n"
        "physics_schemes:\n"
        "  # List active schemes here.\n"
        "cece_data:\n"
        "  # Configure input streams here.\n"
        "```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown)])

    assert result == 0
    assert capsys.readouterr().err == ""


def test_context_directive_reports_fragment_line_not_synthetic_wrapper_line(
    tmp_path, capsys
):
    markdown = tmp_path / "layers.md"
    markdown.write_text(
        "Intro\n"
        "<!-- cece-validate: context species.NO -->\n"
        "```yaml\n"
        "- field: NO_EMIS\n"
        "  operation: add\n"
        "  unknown: true\n"
        "```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown)])
    output = capsys.readouterr().err

    assert result == 1
    assert f"{markdown}:6: species.NO[0]: unknown key 'unknown'" in output


def test_list_fragment_reports_markdown_fence_line(tmp_path, capsys):
    markdown = tmp_path / "guide.md"
    markdown.write_text("Intro\n```yml\n- layer\n```\n", encoding="utf-8")

    result = validator.main([str(markdown)])

    assert result == 1
    assert (
        f"{markdown}:3: expected a CECE configuration mapping"
        in capsys.readouterr().err
    )


def test_speciation_yaml_is_skipped_and_unknown_scheme_is_reported(tmp_path, capsys):
    speciation = tmp_path / "speciation.yaml"
    speciation.write_text("mechanism: cb6\ndatasets: []\n", encoding="utf-8")
    config = tmp_path / "config.yaml"
    config.write_text(
        "physics_schemes:\n  - name: definitely_not_registered\n",
        encoding="utf-8",
    )

    result = validator.main([str(speciation), str(config)])

    assert result == 1
    assert f"{config}:2: unregistered physics scheme" in capsys.readouterr().err


def test_registered_scheme_names_are_scraped_from_cpp_sources(tmp_path, capsys):
    schemes = validator.discover_registered_schemes()
    config = tmp_path / "known_scheme.yaml"
    config.write_text("physics_schemes:\n  - name: megan\n", encoding="utf-8")

    result = validator.main([str(config)])

    assert {"megan", "native_example"}.issubset(schemes)
    assert result == 0
    assert capsys.readouterr().err == ""


def test_yaml_parse_error_reports_its_source_line(tmp_path, capsys):
    config = tmp_path / "bad.yaml"
    config.write_text("species:\n  CO: [\n", encoding="utf-8")

    result = validator.main([str(config)])

    assert result == 1
    assert f"{config}:3:" in capsys.readouterr().err


def test_schema_error_reports_unknown_key_line_without_aggregate_heading(
    tmp_path, capsys
):
    config = tmp_path / "bad_schema.yaml"
    config.write_text(
        "species:\n  CO:\n    - field: CO\n      operation: add\n      unknown: true\n",
        encoding="utf-8",
    )

    result = validator.main([str(config)])
    output = capsys.readouterr().err

    assert result == 1
    assert f"{config}:5: species.CO[0]: unknown key 'unknown'" in output
    assert "Invalid configuration:" not in output


@pytest.mark.parametrize(
    "token",
    ["NO", "no", "YES", "on", "OFF", "True", "true", "TRUE", "False", "false", "FALSE"],
)
def test_boolean_species_key_suggests_quoting(tmp_path, capsys, token):
    config = tmp_path / "ambiguous_species.yaml"
    config.write_text(
        f"species:\n  {token}:\n    - field: EMISSION\n      operation: add\n",
        encoding="utf-8",
    )

    result = validator.main([str(config)])
    output = capsys.readouterr().err

    assert result == 1
    assert (
        f"{config}:2: species.{token}: PyYAML interpreted unquoted species key"
        in output
    )
    assert f'quote it as "{token}"' in output
    bool_value = token.lower() in {"yes", "on", "true"}
    assert f"as boolean {str(bool_value).lower()}" in output


def test_markdown_species_key_hint_uses_document_line_numbers(tmp_path, capsys):
    markdown = tmp_path / "guide.md"
    markdown.write_text(
        "Example\n```yaml\nspecies:\n  NO:\n    - field: EMISSION\n"
        "      operation: add\n```\n",
        encoding="utf-8",
    )

    result = validator.main([str(markdown)])
    output = capsys.readouterr().err

    assert result == 1
    assert (
        f"{markdown}:4: species.NO: PyYAML interpreted unquoted species key" in output
    )
