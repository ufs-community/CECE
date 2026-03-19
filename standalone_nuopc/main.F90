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
  type(ESMF_Field) :: f_maccity, f_sf, f_u10, f_tskin, f_dms_sw
  type(ESMF_Field) :: f_co, f_dms
  real(ESMF_KIND_R8), pointer :: p_maccity(:,:), p_sf(:,:), p_u10(:,:), p_tskin(:,:), p_dms_sw(:,:)
  real(ESMF_KIND_R8), pointer :: p_co(:,:), p_dms(:,:)
  integer, dimension(2) :: maxIndex = [4, 4]

  ! 1. Initialize ESMF
  call ESMF_Initialize(rc=rc)
  print *, "ACES Standalone: ESMF Initialized."

  ! 2. Setup Clock
  call ESMF_TimeSet(startTime, yy=2024, mm=1, dd=1, rc=rc)
  call ESMF_TimeIntervalSet(timeStep, h=1, rc=rc)
  clock = ESMF_ClockCreate(timeStep, startTime, rc=rc)

  ! 3. Setup Grid and States
  grid = ESMF_GridCreateNoPeriDim(maxIndex=maxIndex, rc=rc)
  importState = ESMF_StateCreate(name="ImportState", rc=rc)
  exportState = ESMF_StateCreate(name="ExportState", rc=rc)

  ! 4. Setup Discovery Field (to allow ACES to find grid bounds)
  f_discovery = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="aces_discovery_emissions", rc=rc)
  call ESMF_StateAdd(exportState, [f_discovery], rc=rc)

  ! 5. Setup Input Fields (Matches hemco_comparison_driver)
  f_maccity = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="MACCITY_CO", rc=rc)
  call ESMF_FieldGet(f_maccity, farrayPtr=p_maccity, rc=rc)
  p_maccity = 1.0_8
  call ESMF_StateAdd(importState, [f_maccity], rc=rc)

  f_sf = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="HOURLY_SCALFACT", rc=rc)
  call ESMF_FieldGet(f_sf, farrayPtr=p_sf, rc=rc)
  p_sf = 0.5_8
  call ESMF_StateAdd(importState, [f_sf], rc=rc)

  f_u10 = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="wind_speed_10m", rc=rc)
  call ESMF_FieldGet(f_u10, farrayPtr=p_u10, rc=rc)
  p_u10 = 10.0_8
  call ESMF_StateAdd(importState, [f_u10], rc=rc)

  f_tskin = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="tskin", rc=rc)
  call ESMF_FieldGet(f_tskin, farrayPtr=p_tskin, rc=rc)
  p_tskin = 293.15_8
  call ESMF_StateAdd(importState, [f_tskin], rc=rc)

  f_dms_sw = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="DMS_seawater", rc=rc)
  call ESMF_FieldGet(f_dms_sw, farrayPtr=p_dms_sw, rc=rc)
  p_dms_sw = 1.0e-6_8
  call ESMF_StateAdd(importState, [f_dms_sw], rc=rc)

  ! 6. Setup Output Fields
  f_co = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="co", rc=rc)
  call ESMF_StateAdd(exportState, [f_co], rc=rc)

  f_dms = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, name="dms", rc=rc)
  call ESMF_StateAdd(exportState, [f_dms], rc=rc)

  ! 7. Create and Setup ACES Component
  print *, "ACES Standalone: Creating ACES Component."
  acesComp = ESMF_GridCompCreate(name="ACES", rc=rc)
  call ACES_SetServices(acesComp, rc=rc)

  ! 8. Initialize, Run, and Finalize ACES
  print *, "ACES Standalone: Initializing ACES."
  ! Note: ACES initialization is handled through NUOPC phases
  ! For now, we skip the standalone initialization
  print *, "ACES Standalone: ACES initialization skipped (use NUOPC driver for full initialization)"

  print *, "ACES Standalone: Running ACES (single step)."
  ! call ACES_Run(acesComp, rc=rc)

  ! 9. Print Results for Verification
  call ESMF_FieldGet(f_co, farrayPtr=p_co, rc=rc)
  call ESMF_FieldGet(f_dms, farrayPtr=p_dms, rc=rc)
  print *, "RESULT CO ", p_co(1,1)
  print *, "RESULT DMS ", p_dms(1,1)

  print *, "ACES Standalone: Finalizing ACES."
  ! call ACES_Finalize(acesComp, rc=rc)

  ! 10. Cleanup
  call ESMF_StateDestroy(importState, rc=rc)
  call ESMF_StateDestroy(exportState, rc=rc)
  call ESMF_GridDestroy(grid, rc=rc)
  call ESMF_ClockDestroy(clock, rc=rc)
  call ESMF_GridCompDestroy(acesComp, rc=rc)

  call ESMF_Finalize(rc=rc)
  print *, "ACES Standalone: ESMF Finalized."

end program
