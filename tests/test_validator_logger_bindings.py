"""
Tests for pybind11 ConfigValidator and logger bindings (_aces_core module).

Tests ValidateConfig, set_log_level/get_log_level, get_default_execution_space_name,
and get_available_execution_spaces.
"""

import os
import sys
import pytest

# Add the build output directory to the path so _aces_core can be imported
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build", "src", "python", "aces"))

import _aces_core


class TestConfigValidator:
    """Tests for ConfigValidator bindings."""

    def test_validate_valid_yaml(self):
        # The C++ validator expects species as a YAML sequence with name fields
        yaml_str = """
species:
  - name: co
    units: kg/m2/s
"""
        result = _aces_core.ConfigValidator.ValidateConfig(yaml_str)
        assert isinstance(result, _aces_core.ValidationResult)
        assert result.is_valid is True
        assert result.IsValid() is True

    def test_validate_malformed_yaml_raises_value_error(self):
        malformed_yaml = "{{{{not valid yaml: [["
        with pytest.raises(ValueError, match="YAML parse error"):
            _aces_core.ConfigValidator.ValidateConfig(malformed_yaml)

    def test_validation_result_methods(self):
        yaml_str = """
species:
  - name: co
    units: kg/m2/s
"""
        result = _aces_core.ConfigValidator.ValidateConfig(yaml_str)
        assert result.IsValid() is True
        assert result.GetErrorCount() == 0
        assert result.GetWarningCount() >= 0

    def test_validation_result_errors_is_list(self):
        # species as a map (not a list) triggers a validation error
        yaml_str = "species: {}"
        result = _aces_core.ConfigValidator.ValidateConfig(yaml_str)
        assert isinstance(result.errors, list)
        assert isinstance(result.warnings, list)

    def test_validation_error_fields(self):
        """Test that ValidationError objects have the expected readonly attributes."""
        error = _aces_core.ValidationError()
        assert hasattr(error, "field")
        assert hasattr(error, "message")
        assert hasattr(error, "suggestion")


class TestLogLevel:
    """Tests for set_log_level and get_log_level bindings."""

    def test_set_and_get_debug(self):
        _aces_core.set_log_level("DEBUG")
        assert _aces_core.get_log_level() == "DEBUG"

    def test_set_and_get_info(self):
        _aces_core.set_log_level("INFO")
        assert _aces_core.get_log_level() == "INFO"

    def test_set_and_get_warning(self):
        _aces_core.set_log_level("WARNING")
        assert _aces_core.get_log_level() == "WARNING"

    def test_set_and_get_error(self):
        _aces_core.set_log_level("ERROR")
        assert _aces_core.get_log_level() == "ERROR"

    def test_invalid_level_raises_value_error(self):
        with pytest.raises(ValueError, match="Invalid log level"):
            _aces_core.set_log_level("INVALID")

    def test_roundtrip_debug(self):
        """Test set then get round-trip for DEBUG level."""
        _aces_core.set_log_level("DEBUG")
        level = _aces_core.get_log_level()
        assert level == "DEBUG"
        # Restore default
        _aces_core.set_log_level("INFO")


class TestExecutionSpace:
    """Tests for execution space bindings."""

    def test_get_default_execution_space_name_returns_string(self):
        name = _aces_core.get_default_execution_space_name()
        assert isinstance(name, str)
        assert len(name) > 0

    def test_get_available_execution_spaces_returns_list(self):
        spaces = _aces_core.get_available_execution_spaces()
        assert isinstance(spaces, list)
        assert len(spaces) > 0

    def test_default_space_in_available_spaces(self):
        default_name = _aces_core.get_default_execution_space_name()
        available = _aces_core.get_available_execution_spaces()
        assert default_name in available


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
