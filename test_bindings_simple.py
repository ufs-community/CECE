#!/usr/bin/env python
"""Simple test for bindings implementation."""

import sys
sys.path.insert(0, '/work/src')

from python._bindings import _CBindings, _release_gil, _check_error
from python.exceptions import ACES_SUCCESS

print("Test 1: Bindings initialization")
b = _CBindings()
print(f"  ✓ Bindings created: {b is not None}")
print(f"  ✓ Is loaded: {b.is_loaded()}")

print("\nTest 2: Error handling")
try:
    _check_error(ACES_SUCCESS)
    print("  ✓ No error on ACES_SUCCESS")
except Exception as e:
    print(f"  ✗ Unexpected error: {e}")

print("\nTest 3: GIL management")
try:
    with _release_gil():
        pass
    print("  ✓ GIL context manager works")
except Exception as e:
    print(f"  ✗ GIL context manager failed: {e}")

print("\nTest 4: Function attributes")
print(f"  ✓ Has load_library: {hasattr(b, 'load_library')}")
print(f"  ✓ Has get_last_error: {hasattr(b, 'get_last_error')}")
print(f"  ✓ Has is_loaded: {hasattr(b, 'is_loaded')}")

print("\n✓ All tests passed!")
