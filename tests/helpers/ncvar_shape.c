/* SPDX-License-Identifier: Apache-2.0
 *
 * ncvar_shape: prints the name, rank, and per-dimension names/sizes of one
 * variable in a NetCDF file. Used by the NUOPC cap grid-topology harness
 * (tests/test_driver_nuopc_gridspec.sh) to assert that a curvilinear run
 * carries 2-D coordinate variables: rank and dimension names are
 * the observable proof that the output grid geometry matches the input
 * grid, independent of the coordinate values themselves.
 *
 * Usage: ncvar_shape <file> <var>
 * Output: "<var> rank=<n> dims=<d1:size> <d2:size> ..." on stdout.
 * Exit codes: 0 success, 2 usage/IO/NetCDF error (var not found prints
 * "MISSING" and returns 1).
 */
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: ncvar_shape <file> <var>\n");
        return 2;
    }
    int ncid = -1;
    int status = nc_open(argv[1], NC_NOWRITE, &ncid);
    if (status != NC_NOERR) {
        fprintf(stderr, "ncvar_shape: %s: %s\n", argv[1], nc_strerror(status));
        return 2;
    }
    int varid = -1;
    status = nc_inq_varid(ncid, argv[2], &varid);
    if (status != NC_NOERR) {
        printf("MISSING\n");
        nc_close(ncid);
        return 1;
    }
    int rank = 0;
    status = nc_inq_varndims(ncid, varid, &rank);
    if (status != NC_NOERR) {
        fprintf(stderr, "ncvar_shape: nc_inq_varndims: %s\n", nc_strerror(status));
        nc_close(ncid);
        return 2;
    }
    printf("%s rank=%d", argv[2], rank);
    if (rank > 0) {
        int dimids[NC_MAX_VAR_DIMS];
        status = nc_inq_vardimid(ncid, varid, dimids);
        if (status != NC_NOERR) {
            fprintf(stderr, "ncvar_shape: nc_inq_vardimid: %s\n", nc_strerror(status));
            nc_close(ncid);
            return 2;
        }
        for (int d = 0; d < rank; ++d) {
            char dimname[NC_MAX_NAME + 1];
            size_t len = 0;
            status = nc_inq_dim(ncid, dimids[d], dimname, &len);
            if (status != NC_NOERR) {
                fprintf(stderr, "ncvar_shape: nc_inq_dim: %s\n", nc_strerror(status));
                nc_close(ncid);
                return 2;
            }
            printf(" dims=%s:%zu", dimname, len);
        }
    }
    printf("\n");
    nc_close(ncid);
    return 0;
}
