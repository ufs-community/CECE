"""
State management classes for ACES.

Provides AcesState and AcesField for managing import/export fields.
"""

from typing import Dict, Tuple, Optional
import numpy as np


class AcesField:
    """Represents a single field in ACES state."""

    def __init__(self, name, array, layout="fortran"):
        """
        Initialize field.

        Args:
            name: Field name
            array: Numpy array
            layout: Memory layout ("fortran" or "c")
        """
        self.name = name
        self.array = array
        self.layout = layout
        self._kokkos_view = None

    @property
    def shape(self):
        """Get array shape (nx, ny, nz)."""
        return self.array.shape

    @property
    def dtype(self):
        """Get array data type."""
        return self.array.dtype

    def to_kokkos_view(self):
        """Get Kokkos View (lazy initialization)."""
        # Implementation in subsequent tasks
        return self._kokkos_view


class FieldDict:
    """Dictionary-like access to fields."""

    def __init__(self, fields_dict):
        """
        Initialize field dictionary.

        Args:
            fields_dict: Dict mapping field names to AcesField objects
        """
        self._fields = fields_dict

    def __getitem__(self, name):
        """Get field by name."""
        if name not in self._fields:
            raise KeyError(f"Field '{name}' not found. Available fields: {list(self._fields.keys())}")
        return self._fields[name].array

    def __setitem__(self, name, value):
        """Set field by name."""
        if not isinstance(value, np.ndarray):
            raise TypeError("Field value must be a numpy array")
        self._fields[name] = AcesField(name, value)

    def __contains__(self, name):
        """Check if field exists."""
        return name in self._fields

    def __iter__(self):
        """Iterate over field names."""
        return iter(self._fields)

    def keys(self):
        """Get field names."""
        return self._fields.keys()

    def values(self):
        """Get field arrays."""
        return [f.array for f in self._fields.values()]

    def items(self):
        """Get (name, array) pairs."""
        return [(name, f.array) for name, f in self._fields.items()]

    def get(self, name, default=None):
        """Get field with default."""
        if name in self._fields:
            return self._fields[name].array
        return default


class AcesState:
    """Container for import and export fields."""

    def __init__(self, nx, ny, nz):
        """
        Create state with dimensions.

        Args:
            nx: X dimension
            ny: Y dimension
            nz: Z dimension

        Raises:
            ValueError: If dimensions are invalid
        """
        if nx <= 0 or ny <= 0 or nz <= 0:
            raise ValueError(f"Dimensions must be positive: got ({nx}, {ny}, {nz})")

        self.nx = nx
        self.ny = ny
        self.nz = nz
        self._import_fields = {}
        self._export_fields = {}
        self._import_fields_dict = FieldDict(self._import_fields)
        self._export_fields_dict = FieldDict(self._export_fields)

    def add_import_field(self, name, array):
        """
        Add import field.

        Args:
            name: Field name
            array: Numpy array or array-like

        Raises:
            TypeError: If array type is invalid
            ValueError: If array dimensions don't match state
        """
        # Convert to numpy array if needed
        if isinstance(array, (list, tuple)):
            array = np.array(array, dtype=np.float64)
        elif not isinstance(array, np.ndarray):
            raise TypeError(f"Array must be numpy array or list/tuple, got {type(array)}")

        # Ensure float64
        if array.dtype != np.float64:
            array = array.astype(np.float64)

        # Check dimensions
        if array.shape != (self.nx, self.ny, self.nz):
            raise ValueError(
                f"Array dimensions {array.shape} don't match state dimensions ({self.nx}, {self.ny}, {self.nz})"
            )

        # Determine layout
        if array.flags["F_CONTIGUOUS"]:
            layout = "fortran"
        elif array.flags["C_CONTIGUOUS"]:
            layout = "c"
            # Convert C-order to Fortran-order
            array = np.asfortranarray(array)
            layout = "fortran"
        else:
            raise ValueError("Array must be C-contiguous or Fortran-contiguous")

        self._import_fields[name] = AcesField(name, array, layout)

    def get_import_field(self, name):
        """
        Get import field.

        Args:
            name: Field name

        Returns:
            Numpy array

        Raises:
            KeyError: If field not found
        """
        if name not in self._import_fields:
            raise KeyError(f"Import field '{name}' not found. Available: {list(self._import_fields.keys())}")
        return self._import_fields[name].array

    def get_export_field(self, name):
        """
        Get export field.

        Args:
            name: Field name

        Returns:
            Numpy array

        Raises:
            KeyError: If field not found
        """
        if name not in self._export_fields:
            raise KeyError(f"Export field '{name}' not found. Available: {list(self._export_fields.keys())}")
        return self._export_fields[name].array

    @property
    def import_fields(self):
        """Get dictionary-like access to import fields."""
        return self._import_fields_dict

    @property
    def export_fields(self):
        """Get dictionary-like access to export fields."""
        return self._export_fields_dict

    @property
    def dimensions(self):
        """Get state dimensions (nx, ny, nz)."""
        return (self.nx, self.ny, self.nz)
