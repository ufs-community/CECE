module aces_cap_mod
  use iso_c_binding
  use ESMF
  use NUOPC
  use NUOPC_Model, modelSS => SetServices
  use NUOPC_Model, only : label_Advertise
  use NUOPC_Model, only : label_RealizeProvided
  use NUOPC_Model, only : model_label_Advance => label_Advance
  use NUOPC_Model, only : model_label_Finalize => label_Finalize
#ifdef ACES_HAS_CDEPS
  use cdeps_inline_mod
#endif
  implicit none

  ! C interface to ACES core
  interface
    subroutine aces_core_advertise(importState, exportState, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: importState, exportState
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_realize(data_ptr, importState, exportState, grid, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr, importState, exportState, grid
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_initialize_p1(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), intent(out) :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_initialize_p2(data_ptr, gcomp, importState, exportState, clock, grid, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr, gcomp, importState, exportState, clock, grid
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_run(data_ptr, importState, exportState, clock, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr, importState, exportState, clock
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_finalize(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

contains

  subroutine ACES_SetServices(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    ! 1. Inherit from NUOPC_Model
    call NUOPC_CompDerive(comp, modelSS, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! 2. Specialize NUOPC_Model phases
    call NUOPC_CompSpecialize(comp, specLabel=label_Advertise, &
      specRoutine=ACES_Advertise, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    call NUOPC_CompSpecialize(comp, specLabel=label_RealizeProvided, &
      specRoutine=ACES_Realize, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! 3. Register multi-phase initialization using NUOPC_CompSetEntryPoint
    ! Phase 3: Core initialization (Kokkos, config, PhysicsFactory)
    call NUOPC_CompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, &
      userRoutine=ACES_InitializeP1, phase=3, phaseLabelList=(/"IPDv00p1"/), rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Phase 4: CDEPS and field binding
    call NUOPC_CompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, &
      userRoutine=ACES_InitializeP2, phase=4, phaseLabelList=(/"IPDv00p2"/), rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! 5. Register Run and Finalize phases using NUOPC_CompSpecialize
    call NUOPC_CompSpecialize(comp, specLabel=model_label_Advance, &
      specRoutine=ACES_Run, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    call NUOPC_CompSpecialize(comp, specLabel=model_label_Finalize, &
      specRoutine=ACES_Finalize, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    rc = ESMF_SUCCESS
  end subroutine

  subroutine ACES_Advertise(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc
    type(ESMF_State) :: importState, exportState
    integer(c_int) :: c_rc

    call NUOPC_ModelGet(comp, importState=importState, exportState=exportState, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Call C++ bridge to parse config and advertise fields
    call aces_core_advertise(transfer(importState, c_null_ptr), &
                             transfer(exportState, c_null_ptr), &
                             c_rc)
    rc = int(c_rc)
  end subroutine

  subroutine ACES_Realize(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc
    type(ESMF_State) :: importState, exportState
    type(ESMF_Grid) :: grid
    integer(c_int) :: c_rc
    type(c_ptr) :: data_ptr

    ! Get import/export states and grid from component
    call ESMF_GridCompGet(comp, importState=importState, exportState=exportState, grid=grid, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Retrieve internal state pointer from Phase 1
    call ESMF_GridCompGetInternalState(comp, data_ptr, rc)
    if (rc /= ESMF_SUCCESS) return

    ! Call C++ bridge to create and allocate export fields
    call aces_core_realize(data_ptr, &
                           transfer(importState, c_null_ptr), &
                           transfer(exportState, c_null_ptr), &
                           transfer(grid, c_null_ptr), &
                           c_rc)
    rc = int(c_rc)
  end subroutine

  subroutine ACES_InitializeP1(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(c_ptr) :: data_ptr

    ! Phase 1 (IPDv00p1): Core initialization
    ! - Initialize Kokkos if not already initialized
    ! - Parse YAML configuration
    ! - Allocate AcesInternalData structure
    ! - Initialize PhysicsFactory and instantiate all physics schemes
    ! - Initialize StackingEngine
    ! - Initialize DiagnosticManager

    call aces_core_initialize_p1(data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) return

    ! Store internal state pointer in GridComp for Phase 2
    call ESMF_GridCompSetInternalState(comp, data_ptr, rc)
    if (rc /= ESMF_SUCCESS) return

    rc = ESMF_SUCCESS
  end subroutine

  subroutine ACES_InitializeP2(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(c_ptr) :: data_ptr
    type(ESMF_Grid) :: grid
    type(ESMF_Field) :: field
    type(ESMF_Mesh) :: mesh
    character(len=ESMF_MAXSTR) :: fieldName

    ! Phase 2 (IPDv00p2): CDEPS and field binding
    ! - Initialize CDEPS_Inline if streams are configured
    ! - Extract grid dimensions from ESMF_Grid
    ! - Allocate default mask (all 1.0)
    ! - Cache field metadata for efficient runtime access

    ! 1. Retrieve internal state pointer from Phase 1
    call ESMF_GridCompGetInternalState(comp, data_ptr, rc)
    if (rc /= ESMF_SUCCESS) return

    ! 2. Get grid from component
    call ESMF_GridCompGet(comp, grid=grid, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! 3. Call C++ bridge for Phase 2 initialization
    call aces_core_initialize_p2(data_ptr, &
                                 transfer(comp, c_null_ptr), &
                                 transfer(importState, c_null_ptr), &
                                 transfer(exportState, c_null_ptr), &
                                 transfer(clock, c_null_ptr), &
                                 transfer(grid, c_null_ptr), &
                                 c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) return

    ! 4. Initialize CDEPS (Direct Fortran call)
#ifdef ACES_HAS_CDEPS
    ! Try to discover mesh from existing fields in exportState
    fieldName = "aces_discovery_emissions"
    call ESMF_StateGet(exportState, trim(fieldName), field, rc=rc)
    if (rc == ESMF_SUCCESS) then
        call ESMF_FieldGet(field, mesh=mesh, rc=rc)
    endif

    ! Standard standalone case: config creates streams file
    call cdeps_inline_init(comp, clock, mesh, "aces_emissions.streams", rc)
#endif

    rc = ESMF_SUCCESS
  end subroutine

  subroutine ACES_Run(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer(c_int) :: c_rc
    type(c_ptr) :: data_ptr

    ! Get import/export states and clock from component
    call ESMF_GridCompGet(comp, importState=importState, exportState=exportState, &
                         clock=clock, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! 1. Retrieve internal state pointer
    call ESMF_GridCompGetInternalState(comp, data_ptr, rc)
    if (rc /= ESMF_SUCCESS) return

    ! 2. Advance CDEPS
#ifdef ACES_HAS_CDEPS
    call cdeps_inline_advance(clock, rc)
    if (rc /= ESMF_SUCCESS) then
        ! We treat CDEPS failure as non-fatal to allow ESMF meteorology to proceed
        rc = ESMF_SUCCESS
    end if
#endif

    ! 3. Call ACES core C bridge for compute
    call aces_core_run(data_ptr, &
                       transfer(importState, c_null_ptr), &
                       transfer(exportState, c_null_ptr), &
                       transfer(clock, c_null_ptr), &
                       c_rc)
    rc = int(c_rc)
  end subroutine

  subroutine ACES_Finalize(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(c_ptr) :: data_ptr

    ! 1. Retrieve internal state pointer
    call ESMF_GridCompGetInternalState(comp, data_ptr, rc)

    ! 2. Call ACES core C bridge for cleanup
    call aces_core_finalize(data_ptr, c_rc)
    rc = int(c_rc)
  end subroutine

end module
