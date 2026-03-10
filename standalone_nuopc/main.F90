program aces_nuopc_driver
  use ESMF
  use NUOPC
  use aces_cap_mod
  implicit none

  integer :: rc
  type(ESMF_GridComp) :: acesComp
  type(ESMF_State) :: importState, exportState
  type(ESMF_Clock) :: clock
  type(ESMF_Time) :: startTime
  type(ESMF_TimeInterval) :: timeStep
  type(ESMF_Grid) :: grid
  type(ESMF_Field) :: f_discovery
  integer, dimension(2) :: maxIndex = [360, 180]

  ! 1. Initialize ESMF
  call ESMF_Initialize(rc=rc)
  print *, "ACES Standalone: ESMF Initialized."

  ! 2. Setup Clock
  call ESMF_TimeSet(startTime, yy=2024, mm=1, dd=1, rc=rc)
  call ESMF_TimeIntervalSet(timeStep, h=1, rc=rc)
  clock = ESMF_ClockCreate(timeStep, startTime, rc=rc)

  ! 3. Setup Grid and Fields
  grid = ESMF_GridCreateNoPeriDim(maxIndex=maxIndex, rc=rc)
  importState = ESMF_StateCreate(name="ImportState", rc=rc)
  exportState = ESMF_StateCreate(name="ExportState", rc=rc)

  ! Only add a discovery field to allow ACES to find the grid/mesh
  f_discovery = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="aces_discovery_emissions", rc=rc)
  call ESMF_StateAdd(exportState, [f_discovery], rc=rc)

  ! 4. Create and Setup ACES Component
  print *, "ACES Standalone: Creating ACES Component."
  acesComp = ESMF_GridCompCreate(name="ACES", rc=rc)
  call ACES_SetServices(acesComp, rc=rc)

  ! 5. Initialize, Run, and Finalize ACES
  print *, "ACES Standalone: Initializing ACES."
  call ACES_Initialize(acesComp, importState, exportState, clock, rc=rc)

  print *, "ACES Standalone: Running ACES (single step)."
  call ACES_Run(acesComp, importState, exportState, clock, rc=rc)

  print *, "ACES Standalone: Finalizing ACES."
  call ACES_Finalize(acesComp, importState, exportState, clock, rc=rc)

  ! 6. Cleanup
  call ESMF_StateDestroy(importState, rc=rc)
  call ESMF_StateDestroy(exportState, rc=rc)
  call ESMF_GridDestroy(grid, rc=rc)
  call ESMF_ClockDestroy(clock, rc=rc)
  call ESMF_GridCompDestroy(acesComp, rc=rc)

  call ESMF_Finalize(rc=rc)
  print *, "ACES Standalone: ESMF Finalized."

end program
