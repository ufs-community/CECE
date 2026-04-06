"""
Low-level C binding wrappers for ACES.

Provides ctypes-based interface to C binding library.
"""

import ctypes
import sys
from typing import Optional, Callable
from contextlib import contextmanager
from pathlib import Path

from .exceptions import error_code_to_exception, ACES_SUCCESS


class _CBindings:
    """Wrapper for C binding library using ctypes."""

    def __init__(self):
        """Initialize C bindings."""
        self._lib = None
        self._loaded = False

    def load_library(self, lib_path: Optional[str] = None) -> None:
        """
        Load C binding library using ctypes.CDLL().

        Args:
            lib_path: Optional path to library. If None, searches standard locations.

        Raises:
            OSError: If library cannot be found
        """
        if self._loaded:
            return

        # Determine library name based on platform
        if sys.platform == "darwin":
            lib_name = "libaces_c_bindings.dylib"
        elif sys.platform == "win32":
            lib_name = "aces_c_bindings.dll"
        else:
            lib_name = "libaces_c_bindings.so"

        # Search paths for library
        search_paths = []
        if lib_path:
            search_paths.append(lib_path)

        # Add common installation locations
        search_paths.extend([
            Path(__file__).parent,  # Same directory as this module
            Path(__file__).parent / "lib",
            Path(__file__).parent.parent / "lib",
            Path(__file__).parent.parent.parent / "lib",
        ])

        # Try to load library from search paths
        last_error = None
        for search_path in search_paths:
            try:
                full_path = Path(search_path) / lib_name
                if full_path.exists():
                    self._lib = ctypes.CDLL(str(full_path))
                    self._setup_function_signatures()
                    self._loaded = True
                    return
            except (OSError, TypeError) as e:
                last_error = e
                continue

        # Try loading without full path (relies on system library path)
        try:
            self._lib = ctypes.CDLL(lib_name)
            self._setup_function_signatures()
            self._loaded = True
            return
        except (OSError, TypeError) as e:
            last_error = e

        # If we get here, library could not be loaded
        raise OSError(
            f"Could not find or load C binding library '{lib_name}'. "
            f"Searched in: {', '.join(str(p) for p in search_paths)}. "
            f"Last error: {last_error}"
        )

    def _setup_function_signatures(self) -> None:
        """Define ctypes function signatures for all C functions."""
        if not self._lib:
            raise RuntimeError("Library not loaded")

        # Helper to set up function signature
        def setup_func(name: str, argtypes: list, restype: type) -> Callable:
            func = getattr(self._lib, name)
            func.argtypes = argtypes
            func.restype = restype
            return func

        # Initialization and finalization
        self.aces_c_initialize = setup_func(
            "aces_c_initialize",
            [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int
        )
        self.aces_c_finalize = setup_func(
            "aces_c_finalize",
            [ctypes.c_void_p],
            ctypes.c_int
        )
        self.aces_c_is_initialized = setup_func(
            "aces_c_is_initialized",
            [ctypes.c_void_p],
            ctypes.c_int
        )

        # State management
        self.aces_c_state_create = setup_func(
            "aces_c_state_create",
            [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)],
            ctypes.c_int
        )
        self.aces_c_state_destroy = setup_func(
            "aces_c_state_destroy",
            [ctypes.c_void_p],
            ctypes.c_int
        )
        self.aces_c_state_add_import_field = setup_func(
            "aces_c_state_add_import_field",
            [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_double),
             ctypes.c_int, ctypes.c_int, ctypes.c_int],
            ctypes.c_int
        )
        self.aces_c_state_get_export_field = setup_func(
            "aces_c_state_get_export_field",
            [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_double)),
             ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)],
            ctypes.c_int
        )

        # Computation
        self.aces_c_compute = setup_func(
            "aces_c_compute",
            [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int],
            ctypes.c_int
        )

        # Configuration
        self.aces_c_config_validate = setup_func(
            "aces_c_config_validate",
            [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)],
            ctypes.c_int
        )
        self.aces_c_config_to_yaml = setup_func(
            "aces_c_config_to_yaml",
            [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p)],
            ctypes.c_int
        )

        # Execution space
        self.aces_c_set_execution_space = setup_func(
            "aces_c_set_execution_space",
            [ctypes.c_char_p],
            ctypes.c_int
        )
        self.aces_c_get_execution_space = setup_func(
            "aces_c_get_execution_space",
            [ctypes.POINTER(ctypes.c_char_p)],
            ctypes.c_int
        )
        self.aces_c_get_available_execution_spaces = setup_func(
            "aces_c_get_available_execution_spaces",
            [ctypes.POINTER(ctypes.c_char_p)],
            ctypes.c_int
        )

        # Error handling
        self.aces_c_get_last_error = setup_func(
            "aces_c_get_last_error",
            [ctypes.POINTER(ctypes.c_char_p)],
            ctypes.c_int
        )
        self.aces_c_free_string = setup_func(
            "aces_c_free_string",
            [ctypes.c_char_p],
            None
        )

        # Logging
        self.aces_c_set_log_level = setup_func(
            "aces_c_set_log_level",
            [ctypes.c_char_p],
            ctypes.c_int
        )

        # Diagnostics
        self.aces_c_get_diagnostics = setup_func(
            "aces_c_get_diagnostics",
            [ctypes.POINTER(ctypes.c_char_p)],
            ctypes.c_int
        )
        self.aces_c_reset_diagnostics = setup_func(
            "aces_c_reset_diagnostics",
            [],
            ctypes.c_int
        )

    def is_loaded(self) -> bool:
        """Check if library is loaded."""
        return self._loaded

    def get_last_error(self) -> Optional[str]:
        """
        Get last error message from C layer.

        Returns:
            Error message string or None if no error
        """
        if not self._loaded:
            return None

        error_msg_ptr = ctypes.c_char_p()
        try:
            error_code = self.aces_c_get_last_error(ctypes.byref(error_msg_ptr))
            if error_code == ACES_SUCCESS and error_msg_ptr.value:
                msg = error_msg_ptr.value.decode("utf-8")
                # Free the C string
                self.aces_c_free_string(error_msg_ptr)
                return msg
        except Exception:
            pass

        return None


# Global bindings instance
_c_bindings = _CBindings()


@contextmanager
def _release_gil():
    """
    Context manager to release Python GIL during C++ execution.

    Ensures GIL is reacquired even if an exception occurs.

    Usage:
        with _release_gil():
            # Long-running C++ code here
            error_code = _c_bindings.aces_c_compute(...)
    """
    # Release the GIL
    allow_threads = ctypes.pythonapi.PyEval_SaveThread
    allow_threads.restype = ctypes.c_void_p
    allow_threads.argtypes = []
    state = allow_threads()

    try:
        yield
    finally:
        # Reacquire the GIL
        restore_threads = ctypes.pythonapi.PyEval_RestoreThread
        restore_threads.restype = None
        restore_threads.argtypes = [ctypes.c_void_p]
        restore_threads(state)


def _check_error(error_code: int) -> None:
    """
    Check error code and raise exception if needed.

    Args:
        error_code: Error code from C function

    Raises:
        Exception: If error_code indicates an error
    """
    if error_code != ACES_SUCCESS:
        error_msg = _c_bindings.get_last_error() or "Unknown error"
        exc = error_code_to_exception(error_code, error_msg)
        if exc:
            raise exc
