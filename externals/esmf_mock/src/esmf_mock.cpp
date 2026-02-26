#include "ESMC.h"
#include <iostream>

extern "C" {

// Note: Function pointer signature matches ESMC.h
int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock*, int*), int phase) {
    return ESMF_SUCCESS;
}

int ESMC_StateGetField(ESMC_State state, const char* name, ESMC_Field* field) {
    if (field) {
        field->ptr = state.ptr;
    }
    return ESMF_SUCCESS;
}

void* ESMC_FieldGetPtr(ESMC_Field field, int localDe, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;
    return field.ptr;
}

}
