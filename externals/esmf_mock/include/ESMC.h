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
typedef void* ESMC_Field;

// Enums
typedef enum {
  ESMF_METHOD_INITIALIZE,
  ESMF_METHOD_RUN,
  ESMF_METHOD_FINALIZE
} ESMC_Method;

// Return codes
#define ESMF_SUCCESS 0

// Function prototypes
// Note: ESMC_VM is NOT passed to the user routine in standard ESMC interface
int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock*, int*), int phase);

// Field and State operations
int ESMC_StateGetField(ESMC_State state, const char* name, ESMC_Field* field);
void* ESMC_FieldGetPtr(ESMC_Field field, int localDe, int* rc);

#ifdef __cplusplus
}
#endif

#endif
