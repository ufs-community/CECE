module aces_cap_mod
  use iso_c_binding
  use ESMF
  use NUOPC
  use cdeps_inline_mod
  implicit none

  ! C interface to ACES core
  interface
    subroutine aces_core_advertise(importState, exportState, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: importState, exportState
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_initialize(data_ptr, importState, exportState, clock, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), intent(out) :: data_ptr
      type(c_ptr), value :: importState, exportState, clock
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

    ! 3. Register standard ESMF entry points
    call ESMF_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, userRoutine=ACES_Initialize, phase=1, rc=rc)
    call ESMF_GridCompSetEntryPoint(comp, ESMF_METHOD_RUN, userRoutine=ACES_Run, phase=1, rc=rc)
    call ESMF_GridCompSetEntryPoint(comp, ESMF_METHOD_FINALIZE, userRoutine=ACES_Finalize, phase=1, rc=rc)

    rc = ESMF_SUCCESS
  end subroutine

  subroutine ACES_Advertise(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc
    type(ESMF_State) :: importState, exportState
    integer(c_int) :: c_rc

    call ESMF_GridCompGet(comp, importState=importState, exportState=exportState, rc=rc)

    ! Call C++ bridge to parse config and advertise fields
    call aces_core_advertise(transfer(importState, c_null_ptr), &
                             transfer(exportState, c_null_ptr), &
                             c_rc)
    rc = int(c_rc)
  end subroutine

  subroutine ACES_Initialize(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(c_ptr) :: data_ptr
    type(ESMF_Field) :: f_discovery
    type(ESMF_Mesh) :: mesh

    ! 1. Call ACES core C bridge
    call aces_core_initialize(data_ptr, &
                              transfer(importState, c_null_ptr), &
                              transfer(exportState, c_null_ptr), &
                              transfer(clock, c_null_ptr), &
                              c_rc)
    rc = int(c_rc)
    if (rc /= 0) return

    ! 2. Store internal state pointer in GridComp
    call ESMF_GridCompSetInternalState(comp, data_ptr, rc)
    if (rc /= ESMF_SUCCESS) return

    ! 3. Initialize CDEPS (Direct Fortran call)
    call ESMF_StateGet(exportState, "aces_discovery_emissions", f_discovery, rc=rc)
    if (rc == ESMF_SUCCESS) then
        call ESMF_FieldGet(f_discovery, mesh=mesh, rc=rc)
        if (rc == ESMF_SUCCESS) then
            call cdeps_inline_init(comp, clock, mesh, "aces_emissions.streams", rc)
        end if
    end if

    rc = ESMF_SUCCESS
  end subroutine

  subroutine ACES_Run(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(c_ptr) :: data_ptr

    ! 1. Retrieve internal state pointer
    call ESMF_GridCompGetInternalState(comp, data_ptr, rc)
    if (rc /= ESMF_SUCCESS) return

    ! 2. Advance CDEPS
    call cdeps_inline_advance(clock, rc)
    if (rc /= ESMF_SUCCESS) return

    ! 3. Call ACES core C bridge for compute
    call aces_core_run(data_ptr, &
                       transfer(importState, c_null_ptr), &
                       transfer(exportState, c_null_ptr), &
                       transfer(clock, c_null_ptr), &
                       c_rc)
    rc = int(c_rc)
  end subroutine

  subroutine ACES_Finalize(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
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
