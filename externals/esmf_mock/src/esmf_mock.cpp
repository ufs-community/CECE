#include "ESMC.h"
#include <iostream>

extern "C" {

int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock, ESMC_VM, int*), int phase) {
    // Dummy implementation
    // std::cout << "ESMC_GridCompSetEntryPoint called for method " << method << std::endl;
    return ESMF_SUCCESS;
}

}
