#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Convert MACCity NetCDF files from [-180, 180] to [0, 360] longitude range

import netCDF4 as nc
import numpy as np
import os


def convert_file(input_path, output_path):
    print(f"Converting {input_path} to {output_path}...")
    if not os.path.exists(input_path):
        print(f"Error: {input_path} does not exist.")
        return False

    with nc.Dataset(input_path, "r") as src, nc.Dataset(
        output_path, "w", format="NETCDF4"
    ) as dst:
        # Copy global attributes
        for name in src.ncattrs():
            dst.setncattr(name, src.getncattr(name))

        # Copy dimensions
        for name, dimension in src.dimensions.items():
            dst.createDimension(
                name, (len(dimension) if not dimension.isunlimited() else None)
            )

        # Copy variables
        for name, variable in src.variables.items():
            # Create variable
            dst_var = dst.createVariable(name, variable.datatype, variable.dimensions)

            # Copy variable attributes
            for attr_name in variable.ncattrs():
                dst_var.setncattr(attr_name, variable.getncattr(attr_name))

            # Handle coordinate shift for 'lon'
            if name == "lon":
                lons = variable[:]
                n_lon = len(lons)
                shift_idx = n_lon // 2

                # Shift coordinates: negative values get +360
                new_lons = np.zeros_like(lons)
                new_lons[:shift_idx] = lons[shift_idx:]  # 0 to 180
                new_lons[shift_idx:] = lons[:shift_idx] + 360.0  # 180 to 360

                dst_var[:] = new_lons
                print(
                    f"  Shifted lon variable (size {n_lon}) to range [{new_lons.min()}, {new_lons.max()}]"
                )

            # Handle data variable 'MACCity' (shift/roll data matching coordinates)
            elif name == "MACCity":
                data = variable[:]
                n_lon = data.shape[-1]
                shift_idx = n_lon // 2

                # Roll data by -shift_idx along the last axis (longitude)
                # This shifts the second half (0 to 180) to the beginning, and the first half (-180 to 0) to the end.
                new_data = np.roll(data, shift=-shift_idx, axis=-1)
                dst_var[:] = new_data
                print(
                    f"  Rolled 'MACCity' variable data (shape {data.shape}) along longitude axis"
                )

            # Copy other variables (lat, time, etc.) verbatim
            else:
                dst_var[:] = variable[:]
                print(f"  Copied variable '{name}' verbatim")

    print(f"Successfully converted {input_path} -> {output_path}")
    return True


if __name__ == "__main__":
    convert_file("data/MACCity_4x5.nc", "data/MACCity_4x5_0_360.nc")
    convert_file(
        "data/MACCity_anthro_NOx_2000-2010_16080.nc",
        "data/MACCity_anthro_NOx_2000-2010_16080_0_360.nc",
    )
