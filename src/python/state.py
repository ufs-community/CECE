"""
State management classes for the CECE Python interface.

Provides ``CeceState`` for managing 3D import and export fields, ``CeceField``
for wrapping individual NumPy arrays with metadata, and ``FieldDict`` for
dictionary-like access to field collections.

Notes
-----
All field arrays are stored as Fortran-contiguous (column-major) float64
NumPy arrays to match the Kokkos ``LayoutLeft`` memory layout used by the
C++ core. C-contiguous arrays are automatically converted on import.

See Also
--------
cece.compute : Execute computation using state fields.
"""

from __future__ import annotations

from typing import Any, Dict, Iterator, List, Optional, Tuple, Union

import numpy as np


class CeceField:
    """
    A single named field in the CECE state.

    Wraps a NumPy array with metadata including field name and memory layout.

    Parameters
    ----------
    name : str
        Field identifier (e.g., ``"TEMPERATURE"``, ``"CO_EMIS"``).
    array : np.ndarray
        3D data array with shape ``(nx, ny, nz)``.
    layout : str, optional
        Memory layout. Either ``"fortran"`` (column-major) or ``"c"``
        (row-major). Default is ``"fortran"``.

    Attributes
    ----------
    name : str
        Field name.
    array : np.ndarray
        Underlying data array.
    layout : str
        Memory layout identifier.
    """

    def __init__(self, name: str, array: np.ndarray, layout: str = "fortran") -> None:
        self.name = name
        self.array = array
        self.layout = layout
        self._kokkos_view: Optional[object] = None

    @property
    def shape(self) -> tuple:
        """tuple : Array shape ``(nx, ny, nz)``."""
        return self.array.shape

    @property
    def dtype(self) -> np.dtype:
        """numpy.dtype : Array data type."""
        return self.array.dtype

    def to_kokkos_view(self) -> Optional[object]:
        """
        Get the Kokkos View associated with this field.

        Returns the lazily-initialized Kokkos View wrapping the array data.
        Currently a placeholder for future Kokkos interop.

        Returns
        -------
        object or None
            Kokkos View object, or ``None`` if not yet initialized.
        """
        return self._kokkos_view


class FieldDict:
    """
    Dictionary-like access to a collection of ``CeceField`` objects.

    Provides ``__getitem__``, ``__setitem__``, ``__contains__``, and
    iteration over field names, returning raw NumPy arrays rather than
    ``CeceField`` wrappers.

    Parameters
    ----------
    fields_dict : dict
        Mapping of field names to ``CeceField`` objects. This dict is
        shared with the owning ``CeceState`` and mutated in place.
    """

    def __init__(self, fields_dict: Dict[str, CeceField]) -> None:
        self._fields = fields_dict

    def __getitem__(self, name: str) -> np.ndarray:
        """
        Get a field's array by name.

        Parameters
        ----------
        name : str
            Field name.

        Returns
        -------
        np.ndarray
            The field's data array.

        Raises
        ------
        KeyError
            If the field name is not found.
        """
        if name not in self._fields:
            raise KeyError(
                f"Field '{name}' not found. Available fields: {list(self._fields.keys())}"
            )
        return self._fields[name].array

    def __setitem__(self, name: str, value: np.ndarray) -> None:
        """
        Set a field by name.

        Parameters
        ----------
        name : str
            Field name.
        value : np.ndarray
            Array data for the field.

        Raises
        ------
        TypeError
            If ``value`` is not a NumPy array.
        """
        if not isinstance(value, np.ndarray):
            raise TypeError("Field value must be a numpy array")
        self._fields[name] = CeceField(name, value)

    def __contains__(self, name: object) -> bool:
        """
        Check if a field exists.

        Parameters
        ----------
        name : str
            Field name to check.

        Returns
        -------
        bool
            ``True`` if the field exists.
        """
        return name in self._fields

    def __iter__(self) -> Iterator[str]:
        """
        Iterate over field names.

        Yields
        ------
        str
            Field names.
        """
        return iter(self._fields)

    def keys(self) -> Any:
        """
        Get all field names.

        Returns
        -------
        dict_keys
            View of field names.
        """
        return self._fields.keys()

    def values(self) -> List[np.ndarray]:
        """
        Get all field arrays.

        Returns
        -------
        list of np.ndarray
            List of data arrays for all fields.
        """
        return [f.array for f in self._fields.values()]

    def items(self) -> List[Tuple[str, np.ndarray]]:
        """
        Get ``(name, array)`` pairs for all fields.

        Returns
        -------
        list of tuple
            List of ``(str, np.ndarray)`` pairs.
        """
        return [(name, f.array) for name, f in self._fields.items()]

    def get(self, name: str, default: Any = None) -> Any:
        """
        Get a field's array with a default fallback.

        Parameters
        ----------
        name : str
            Field name.
        default : object, optional
            Value to return if the field is not found. Default is ``None``.

        Returns
        -------
        np.ndarray or object
            The field's data array, or ``default`` if not found.
        """
        if name in self._fields:
            return self._fields[name].array
        return default


class CeceState:
    """
    Container for CECE import and export fields.

    Manages 3D field arrays for a fixed grid with dimensions ``(nx, ny, nz)``.
    Import fields are provided by the user before computation; export fields
    are populated by the CECE compute engine.

    Parameters
    ----------
    nx : int
        X (longitude) dimension size. Must be positive.
    ny : int
        Y (latitude) dimension size. Must be positive.
    nz : int
        Z (vertical) dimension size. Must be positive.

    Attributes
    ----------
    nx : int
        X dimension.
    ny : int
        Y dimension.
    nz : int
        Z dimension.

    Raises
    ------
    ValueError
        If any dimension is not positive.

    Examples
    --------
    >>> state = CeceState(nx=144, ny=96, nz=72)
    >>> state.add_import_field("TEMPERATURE", temp_array)
    >>> state.dimensions
    (144, 96, 72)
    """

    def __init__(self, nx: int, ny: int, nz: int) -> None:
        if nx <= 0 or ny <= 0 or nz <= 0:
            raise ValueError(f"Dimensions must be positive: got ({nx}, {ny}, {nz})")

        self.nx: int = nx
        self.ny: int = ny
        self.nz: int = nz
        self._import_fields: Dict[str, CeceField] = {}
        self._export_fields: Dict[str, CeceField] = {}
        self._import_fields_dict: FieldDict = FieldDict(self._import_fields)
        self._export_fields_dict: FieldDict = FieldDict(self._export_fields)

    def add_import_field(
        self, name: str, array: Union[np.ndarray, list, tuple]
    ) -> None:
        """
        Add an import field to the state.

        The array is converted to float64 and Fortran-contiguous layout if
        necessary to match the Kokkos ``LayoutLeft`` memory order.

        Parameters
        ----------
        name : str
            Field name (e.g., ``"TEMPERATURE"``, ``"PRESSURE"``).
        array : np.ndarray or list or tuple
            3D data array with shape ``(nx, ny, nz)``. Lists and tuples are
            converted to NumPy arrays automatically.

        Raises
        ------
        TypeError
            If ``array`` is not a NumPy array, list, or tuple.
        ValueError
            If array dimensions do not match ``(nx, ny, nz)`` or the array
            is not contiguous.
        """
        # Convert to numpy array if needed
        if isinstance(array, (list, tuple)):
            array = np.array(array, dtype=np.float64)
        elif not isinstance(array, np.ndarray):
            raise TypeError(
                f"Array must be numpy array or list/tuple, got {type(array)}"
            )

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

        self._import_fields[name] = CeceField(name, array, layout)

    def get_import_field(self, name: str) -> np.ndarray:
        """
        Get an import field's data array.

        Parameters
        ----------
        name : str
            Field name.

        Returns
        -------
        np.ndarray
            The field's 3D data array.

        Raises
        ------
        KeyError
            If the field name is not found in import fields.
        """
        if name not in self._import_fields:
            raise KeyError(
                f"Import field '{name}' not found. Available: {list(self._import_fields.keys())}"
            )
        return self._import_fields[name].array

    def get_export_field(self, name: str) -> np.ndarray:
        """
        Get an export field's data array.

        Parameters
        ----------
        name : str
            Field name.

        Returns
        -------
        np.ndarray
            The field's 3D data array populated by the compute engine.

        Raises
        ------
        KeyError
            If the field name is not found in export fields.
        """
        if name not in self._export_fields:
            raise KeyError(
                f"Export field '{name}' not found. Available: {list(self._export_fields.keys())}"
            )
        return self._export_fields[name].array

    @property
    def import_fields(self) -> FieldDict:
        """FieldDict : Dictionary-like access to import fields."""
        return self._import_fields_dict

    @property
    def export_fields(self) -> FieldDict:
        """FieldDict : Dictionary-like access to export fields."""
        return self._export_fields_dict

    @property
    def dimensions(self) -> Tuple[int, int, int]:
        """tuple : Grid dimensions ``(nx, ny, nz)``."""
        return (self.nx, self.ny, self.nz)
