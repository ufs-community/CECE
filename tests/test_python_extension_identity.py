"""Legacy and package imports must share one native module/type registry."""
import importlib


def test_extension_has_single_identity():
    import cece
    legacy = importlib.import_module('_cece_core')
    package = importlib.import_module('cece._cece_core')
    assert legacy is package is cece._cece_core
