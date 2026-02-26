#include "ESMC.h"
#include <iostream>

extern "C" {

// Note: Function pointer signature matches ESMC.h: (GridComp, State, State, Clock*, int*)
int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock*, int*), int phase) {
    // Dummy implementation
    // std::cout << "ESMC_GridCompSetEntryPoint called for method " << method << std::endl;
    return ESMF_SUCCESS;
}

int ESMC_StateGetField(ESMC_State state, const char* name, ESMC_Field* field) {
    // In a real mock we would look up the field in the state.
    // For now, we just return success and a dummy pointer.
    if (field) *field = (ESMC_Field)0xdeadbeef;
    return ESMF_SUCCESS;
}

void* ESMC_FieldGetPtr(ESMC_Field field, int localDe, int *rc) {
    // Dummy implementation
    if (rc) *rc = ESMF_SUCCESS;
    return nullptr;
}

}
