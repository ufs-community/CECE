/* SPDX-License-Identifier: Apache-2.0
 * CECE - Chemical Emissions Coupling Engine
 *
 * nccmp_var: a tiny, dependency-light NetCDF variable comparator used by the
 * distributed-domain-decomposition Task 10.2 integration equivalence harness
 * (tests/test_distributed_output_equivalence.sh).
 *
 *   Feature: distributed-domain-decomposition
 *   Property 5: Distributed output equals replicated output
 *   Property 6: Single-rank output equals multi-rank output
 *
 * It reads a single named variable from two NetCDF files as double, checks that
 * the shapes match, and asserts the values are bit-for-bit identical (default)
 * or within an absolute tolerance if one is supplied. It is compiled inside the
 * cece-dev container against the container's NetCDF-C (the same library the
 * writer uses), which is the most robust way to read the emitted `oc` field
 * without ncdump/nccmp/python (none of which the image ships).
 *
 * Usage:
 *   nccmp_var <fileA> <fileB> <varname> [abs_tol]
 *
 * Exit codes:
 *   0  variable present in both, shapes match, values equal (bit-for-bit) or
 *      within abs_tol -> EQUIVALENT
 *   1  values differ beyond tolerance -> NOT equivalent
 *   2  usage / IO / shape / NetCDF error
 */
#include <math.h>
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nc_die(const char* what, int status) {
    fprintf(stderr, "nccmp_var: %s: %s\n", what, nc_strerror(status));
    return 2;
}

/* Read the whole named variable from `path` into a freshly malloc'd double
 * buffer. Returns the element count via *n_out and the malloc'd buffer via
 * *buf_out (caller frees). Returns 0 on success, 2 on error. */
static int read_var_as_double(const char* path, const char* varname, double** buf_out, size_t* n_out) {
    int ncid = -1;
    int status = nc_open(path, NC_NOWRITE, &ncid);
    if (status != NC_NOERR) {
        return nc_die(path, status);
    }

    int varid = -1;
    status = nc_inq_varid(ncid, varname, &varid);
    if (status != NC_NOERR) {
        nc_close(ncid);
        return nc_die("nc_inq_varid", status);
    }

    int ndims = 0;
    status = nc_inq_varndims(ncid, varid, &ndims);
    if (status != NC_NOERR) {
        nc_close(ncid);
        return nc_die("nc_inq_varndims", status);
    }

    int dimids[NC_MAX_VAR_DIMS];
    status = nc_inq_vardimid(ncid, varid, dimids);
    if (status != NC_NOERR) {
        nc_close(ncid);
        return nc_die("nc_inq_vardimid", status);
    }

    size_t n = 1;
    for (int d = 0; d < ndims; ++d) {
        size_t len = 0;
        status = nc_inq_dimlen(ncid, dimids[d], &len);
        if (status != NC_NOERR) {
            nc_close(ncid);
            return nc_die("nc_inq_dimlen", status);
        }
        n *= len;
    }

    double* buf = (double*)malloc(n * sizeof(double));
    if (buf == NULL) {
        fprintf(stderr, "nccmp_var: out of memory allocating %zu doubles\n", n);
        nc_close(ncid);
        return 2;
    }

    /* nc_get_var_double transparently converts the stored type to double. */
    status = nc_get_var_double(ncid, varid, buf);
    if (status != NC_NOERR) {
        free(buf);
        nc_close(ncid);
        return nc_die("nc_get_var_double", status);
    }

    nc_close(ncid);
    *buf_out = buf;
    *n_out = n;
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "usage: %s <fileA> <fileB> <varname> [abs_tol]\n", argv[0]);
        return 2;
    }

    const char* file_a = argv[1];
    const char* file_b = argv[2];
    const char* varname = argv[3];
    double abs_tol = 0.0;
    if (argc == 5) {
        abs_tol = strtod(argv[4], NULL);
    }

    double* a = NULL;
    double* b = NULL;
    size_t na = 0;
    size_t nb = 0;

    int rc = read_var_as_double(file_a, varname, &a, &na);
    if (rc != 0) {
        return rc;
    }
    rc = read_var_as_double(file_b, varname, &b, &nb);
    if (rc != 0) {
        free(a);
        return rc;
    }

    if (na != nb) {
        fprintf(stderr, "nccmp_var: element count mismatch for '%s': %zu vs %zu\n", varname, na, nb);
        free(a);
        free(b);
        return 2;
    }

    size_t first_diff = (size_t)-1;
    double max_abs_diff = 0.0;
    size_t n_diff = 0;
    /* Field statistics for file A, used to guard against a VACUOUS equivalence
     * (two all-zero / all-fill fields would trivially compare equal). */
    size_t a_nonzero = 0;
    double a_sum = 0.0;
    double a_max = 0.0;
    for (size_t i = 0; i < na; ++i) {
        double diff = fabs(a[i] - b[i]);
        if (diff > max_abs_diff) {
            max_abs_diff = diff;
        }
        if (a[i] != 0.0) {
            ++a_nonzero;
            a_sum += a[i];
            if (fabs(a[i]) > fabs(a_max)) a_max = a[i];
        }
        int differs;
        if (abs_tol > 0.0) {
            differs = (diff > abs_tol);
        } else {
            /* Default: require bit-for-bit identical values. */
            differs = (a[i] != b[i]);
        }
        if (differs) {
            ++n_diff;
            if (first_diff == (size_t)-1) {
                first_diff = i;
            }
        }
    }

    if (n_diff == 0) {
        printf("nccmp_var: EQUIVALENT var='%s' n=%zu max_abs_diff=%.3e tol=%.3e (%s) "
               "fieldA_nonzero=%zu fieldA_sum=%.6e fieldA_max=%.6e\n",
               varname, na, max_abs_diff, abs_tol,
               abs_tol > 0.0 ? "within-tol" : "bit-for-bit", a_nonzero, a_sum, a_max);
        if (a_nonzero == 0) {
            fprintf(stderr, "nccmp_var: WARNING field '%s' in file A is entirely ZERO -> "
                            "equivalence may be vacuous\n", varname);
        }
        free(a);
        free(b);
        return 0;
    }

    fprintf(stderr,
            "nccmp_var: DIFFER var='%s' n=%zu ndiff=%zu first_diff_idx=%zu a=%.17g b=%.17g max_abs_diff=%.3e tol=%.3e\n",
            varname, na, n_diff, first_diff, a[first_diff], b[first_diff], max_abs_diff, abs_tol);
    free(a);
    free(b);
    return 1;
}
