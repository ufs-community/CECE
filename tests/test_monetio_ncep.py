"""Offline tests against MONETIO's pinned NCEP GRIB fixture and readers."""
from pathlib import Path

import numpy as np
import pytest

from cece.sources import MonetioSource


@pytest.mark.parametrize('reader', ['gfs', 'gefs'])
def test_ncep_scalar_valid_time_is_bounded(reader):
    pytest.importorskip('monetio')
    pytest.importorskip('grib2io')
    path = Path(__file__).parent / 'data' / 'monetio-ncep' / 'sample.grib2'
    with MonetioSource(reader, files=str(path), variables=['mslp'], tolerance='1h') as source:
        valid = np.datetime64(source._delegate._dataset.validDate.values)
        result = source.read(valid)
        assert result.mslp.shape == (181, 360)
        with pytest.raises(ValueError, match='tolerance'):
            source.read(valid + np.timedelta64(1, 'D'))
