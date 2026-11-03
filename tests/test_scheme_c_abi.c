#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "cece/cece_scheme_c_abi.h"
int main(void) {
    char error[512];
    cece_scheme_handle handle = 0;
    assert(cece_scheme_create("native_example", "multiplier: 3.0", 2, 1, &handle, error, 512) == 0);
    double input[2] = {2, 4}, output[2] = {-1, -1};
    assert(cece_scheme_set_field(handle, "base_anthropogenic_nox", input, 2, 1, error, 512) == 0);
    assert(cece_scheme_step(handle, 0, 3600, error, 512) == 0);
    assert(cece_scheme_get_field(handle, "nox", output, 2, error, 512) == 0);
    assert(output[0] == 6 && output[1] == 12);
    assert(cece_scheme_set_field(handle, "base_anthropogenic_nox", NULL, 2, 1, error, 512) != 0);
    assert(cece_scheme_destroy(handle, error, 512) == 0);
    assert(cece_scheme_destroy(handle, error, 512) != 0);
    assert(cece_scheme_step(handle, 3600, 3600, error, 512) != 0);
    assert(cece_scheme_finalize_runtime(error, 512) == 0);
    puts("C scheme ABI checks passed");
    return 0;
}
