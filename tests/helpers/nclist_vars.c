/* SPDX-License-Identifier: Apache-2.0
 *
 * nclist_vars: prints the names of all variables in a NetCDF file, one per
 * line. Companion to nccmp_var.c for the driver-parity harness
 * (tests/test_driver_nuopc_parity.sh), which must assert that the OUTPUT
 * VARIABLE SET equals the configuration's data fields, not merely
 * that a named field matches between two files.
 *
 * Usage: nclist_vars <file>
 * Exit codes: 0 success, 2 usage/IO/NetCDF error.
 */
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: nclist_vars <file>\n");
        return 2;
    }
    int ncid = -1;
    int status = nc_open(argv[1], NC_NOWRITE, &ncid);
    if (status != NC_NOERR) {
        fprintf(stderr, "nclist_vars: %s: %s\n", argv[1], nc_strerror(status));
        return 2;
    }
    int nvars = 0;
    status = nc_inq_nvars(ncid, &nvars);
    if (status != NC_NOERR) {
        fprintf(stderr, "nclist_vars: nc_inq_nvars: %s\n", nc_strerror(status));
        nc_close(ncid);
        return 2;
    }
    for (int v = 0; v < nvars; ++v) {
        char name[NC_MAX_NAME + 1];
        status = nc_inq_varname(ncid, v, name);
        if (status != NC_NOERR) {
            fprintf(stderr, "nclist_vars: nc_inq_varname(%d): %s\n", v, nc_strerror(status));
            nc_close(ncid);
            return 2;
        }
        printf("%s\n", name);
    }
    nc_close(ncid);
    return 0;
}
