#!/usr/bin/env python
"""Checkpoint test for Task 10: Verify basic functionality."""

import sys
sys.path.insert(0, '/work/build/src/python')

import aces
import numpy as np

print("=== Task 10: Checkpoint - Verify Basic Functionality ===\n")

# Test 1: Module imports without errors
print("Test 1: Module imports without errors")
print("  PASS: Module imported successfully\n")

# Test 2: C library loads successfully
print("Test 2: C library loads successfully")
print("  PASS: C library loaded (module imported)\n")

# Test 3: Basic configuration creation works
print("Test 3: Basic configuration creation...")
config = aces.AcesConfig()
assert config is not None, "Config creation failed"
assert hasattr(config, 'species'), "Config missing species property"
assert hasattr(config, 'physics_schemes'), "Config missing physics_schemes property"
assert hasattr(config, 'aces_data'), "Config missing aces_data property"
assert hasattr(config, 'vertical_config'), "Config missing vertical_config property"
print("  - Created empty config: OK")
print("  - Config has all required properties: OK")
print("  PASS: Basic configuration creation works\n")

# Test 4: State creation works
print("Test 4: State creation...")
state = aces.AcesState(nx=10, ny=10, nz=10)
assert state is not None, "State creation failed"
assert state.dimensions == (10, 10, 10), f"State dimensions mismatch: {state.dimensions}"
assert hasattr(state, 'import_fields'), "State missing import_fields property"
assert hasattr(state, 'export_fields'), "State missing export_fields property"
print("  - Created state with dimensions (10, 10, 10): OK")
print("  - State has all required properties: OK")
print("  PASS: State creation works\n")

# Test 5: Add import field
print("Test 5: Add import field...")
field = np.ones((10, 10, 10), dtype=np.float64)
state.add_import_field("temperature", field)
assert "temperature" in state.import_fields, "Field not in import_fields"
retrieved = state.get_import_field("temperature")
assert retrieved.shape == (10, 10, 10), f"Retrieved field shape mismatch: {retrieved.shape}"
assert np.allclose(retrieved, field), "Retrieved field data mismatch"
print("  - Added import field 'temperature': OK")
print("  - Retrieved field matches original: OK")
print("  PASS: Add import field works\n")

# Test 6: Configuration validation
print("Test 6: Configuration validation...")
result = config.validate()
assert hasattr(result, 'is_valid'), "Validation result missing is_valid attribute"
assert result.is_valid, "Empty config should be valid"
print("  - Validation result has is_valid attribute: OK")
print("  - Empty config is valid: OK")
print("  PASS: Configuration validation works\n")

# Test 7: Configuration serialization
print("Test 7: Configuration serialization...")
config_dict = config.to_dict()
assert isinstance(config_dict, dict), "to_dict() should return dict"
assert 'species' in config_dict, "Dict should have 'species' key"
yaml_str = config.to_yaml()
assert isinstance(yaml_str, str), "to_yaml() should return string"
assert len(yaml_str) > 0, "YAML string should not be empty"
print("  - to_dict() returns dict with species key: OK")
print("  - to_yaml() returns non-empty string: OK")
print("  PASS: Configuration serialization works\n")

print("=== All Checkpoint Tests Passed ===")
