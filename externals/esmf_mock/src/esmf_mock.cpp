#include "ESMC.h"
#include <iostream>
#include <map>

// Global storage for mock internal data since ESMC handles are passed by value
static std::map<void*, void*> gridcomp_internal_data;

extern "C" {

// Note: Function pointer signature matches ESMC.h
int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock*, int*), int phase) {
    return ESMF_SUCCESS;
}

int ESMC_GridCompSetInternalState(ESMC_GridComp comp, void* data) {
    gridcomp_internal_data[comp.ptr] = data;
    return ESMF_SUCCESS;
}

void* ESMC_GridCompGetInternalState(ESMC_GridComp comp, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;
    return gridcomp_internal_data[comp.ptr];
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

int ESMC_FieldGetBounds(ESMC_Field field, int* exclusiveLBound, int* exclusiveUBound, int* exclusiveCount, int rank) {
    if (exclusiveLBound) {
        for (int i=0; i<rank; ++i) exclusiveLBound[i] = 1;
    }
    if (exclusiveUBound) {
        // Return dummy bounds
        if (rank >= 1) exclusiveUBound[0] = 360;
        if (rank >= 2) exclusiveUBound[1] = 180;
        if (rank >= 3) exclusiveUBound[2] = 72;
    }
    return ESMF_SUCCESS;
}

}
