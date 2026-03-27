import netCDF4
import numpy as np

try:
    nc = netCDF4.Dataset('build/output/aces_output_20200101_000000.nc')
    print("Variables:", nc.variables.keys())
    co = nc.variables['co'][:]
    print("CO shape:", co.shape)
    print("CO min:", np.min(co))
    print("CO max:", np.max(co))
    print("CO mean:", np.mean(co))

    if np.allclose(co, 1.0):
        print("SUCCESS: CO is all 1.0 as expected from dummy data.")
    else:
        print("FAILURE: CO values are not 1.0.")

    nc.close()
except Exception as e:
    print(f"Error checking NetCDF: {e}")
