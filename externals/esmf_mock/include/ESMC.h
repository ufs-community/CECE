#ifndef ESMC_H
#define ESMC_H

#ifdef __cplusplus
extern "C" {
#endif

// Typedefs for handles - these are structs in real ESMF
typedef struct { void* ptr; } ESMC_GridComp;
typedef struct { void* ptr; } ESMC_State;
typedef struct { void* ptr; } ESMC_Clock;
typedef struct { void* ptr; } ESMC_VM;
typedef struct { void* ptr; } ESMC_Field;

// Enums
typedef enum {
  ESMF_METHOD_INITIALIZE,
  ESMF_METHOD_RUN,
  ESMF_METHOD_FINALIZE
} ESMC_Method;

// Return codes
#define ESMF_SUCCESS 0

// Function prototypes
int ESMC_GridCompSetEntryPoint(ESMC_GridComp comp, ESMC_Method method, void (*function)(ESMC_GridComp, ESMC_State, ESMC_State, ESMC_Clock*, int*), int phase);
int ESMC_GridCompSetInternalState(ESMC_GridComp comp, void* data);
void* ESMC_GridCompGetInternalState(ESMC_GridComp comp, int* rc);

// Field and State operations
int ESMC_StateGetField(ESMC_State state, const char* name, ESMC_Field* field);
void* ESMC_FieldGetPtr(ESMC_Field field, int localDe, int* rc);
int ESMC_FieldGetBounds(ESMC_Field field, int* exclusiveLBound, int* exclusiveUBound, int* exclusiveCount, int rank);

#ifdef __cplusplus
}
#endif

#endif
