#include "ESMC.h"
#include <iostream>

extern "C" {

// Note: Function pointer signature matches ESMC.h: (GridComp, State, State, Clock*, int*)
int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock*, int*), int phase) {
    // Dummy implementation
    // std::cout << "ESMC_GridCompSetEntryPoint called for method " << method << std::endl;
    return ESMF_SUCCESS;
}

ESMC_Field ESMC_StateGetField(ESMC_State state, const char* name, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;
    return (ESMC_Field)state;
}

void* ESMC_FieldGetPtr(ESMC_Field field, int localDe, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;
    return (void*)field;
}

}
