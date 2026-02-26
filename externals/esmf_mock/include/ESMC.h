#ifndef ESMC_H
#define ESMC_H

#ifdef __cplusplus
extern "C" {
#endif

// Typedefs for handles
typedef void* ESMC_GridComp;
typedef void* ESMC_State;
typedef void* ESMC_Clock;
typedef void* ESMC_VM;

// Enums
typedef enum {
  ESMF_METHOD_INITIALIZE,
  ESMF_METHOD_RUN,
  ESMF_METHOD_FINALIZE
} ESMC_Method;

// Return codes
#define ESMF_SUCCESS 0

// Function prototypes
int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock, ESMC_VM, int*), int phase);

#ifdef __cplusplus
}
#endif

#endif
