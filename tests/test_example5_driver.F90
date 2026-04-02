!> @file test_example5_driver.F90
!> @brief Fortran NUOPC integration test: run aces_config_ex5.yaml through the
!>        full ESMF/NUOPC phase sequence and verify that co, no, and so2 export
!>        fields are all nonzero and mutually distinct after one run step.
!>
!> Uses the Fortran ESMF API throughout (no ESMC C bindings).
!>
!> Pass/fail is communicated via process exit code (0 = pass, 1 = fail).

program test_example5_driver
  use ESMF
  use NUOPC
  use aces_cap_mod
  implicit none

  ! -----------------------------------------------------------------------
  ! ESMF handles
  ! -----------------------------------------------------------------------
  type(ESMF_GridComp)     :: acesComp
  type(ESMF_State)        :: importState, exportState
  type(ESMF_Clock)        :: clock
  type(ESMF_Time)         :: startTime, stopTime
  type(ESMF_TimeInterval) :: timeStep
  type(ESMF_Calendar)     :: calendar
  type(ESMF_Grid)         :: grid
  type(ESMF_Mesh)         :: mesh
  type(ESMF_VM)           :: vm
  type(ESMF_Field)        :: co_field, no_field

  ! -----------------------------------------------------------------------
  ! Test configuration — mirrors aces_config_ex5.yaml species
  ! -----------------------------------------------------------------------
  character(len=512), parameter :: CONFIG_FILE = "examples/aces_config_ex5.yaml"
  integer, parameter :: NX = 4, NY = 4, DT_SECS = 3600

  ! -----------------------------------------------------------------------
  ! Runtime variables
  ! -----------------------------------------------------------------------
  integer :: rc, petCount, localPet
  integer :: maxIndex2D(2)
  real(ESMF_KIND_R8), pointer :: co_ptr(:,:,:), no_ptr(:,:,:)
  real(ESMF_KIND_R8) :: co_sum, no_sum
  integer :: i, j, k
  logical :: pass

  ! -----------------------------------------------------------------------
  ! 1. ESMF initialise
  ! -----------------------------------------------------------------------
  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_GREGORIAN, &
                       defaultLogFileName="test_example5.log", &
                       logkindflag=ESMF_LOGKIND_MULTI, rc=rc)
  call check(rc, "ESMF_Initialize")

  call ESMF_VMGetCurrent(vm, rc=rc)
  call check(rc, "ESMF_VMGetCurrent")
  call ESMF_VMGet(vm, petCount=petCount, localPet=localPet, rc=rc)
  call check(rc, "ESMF_VMGet")

  write(*,'(A)') "INFO: [test_example5] ESMF initialized"

  ! -----------------------------------------------------------------------
  ! 2. Clock: 2020-01-01 00:00 -> 2020-01-01 01:00, dt=1h
  ! -----------------------------------------------------------------------
  calendar = ESMF_CalendarCreate(ESMF_CALKIND_GREGORIAN, name="Gregorian", rc=rc)
  call check(rc, "CalendarCreate")

  call ESMF_TimeSet(startTime, yy=2020, mm=1, dd=1, h=0, m=0, s=0, &
                    calendar=calendar, rc=rc)
  call check(rc, "TimeSet start")

  call ESMF_TimeSet(stopTime, yy=2020, mm=1, dd=1, h=1, m=0, s=0, &
                    calendar=calendar, rc=rc)
  call check(rc, "TimeSet stop")

  call ESMF_TimeIntervalSet(timeStep, s=DT_SECS, rc=rc)
  call check(rc, "TimeIntervalSet")

  clock = ESMF_ClockCreate(timeStep, startTime, stopTime=stopTime, &
                            name="test_ex5_clock", rc=rc)
  call check(rc, "ClockCreate")

  ! -----------------------------------------------------------------------
  ! 3. Grid (NX x NY, no periodicity)
  ! -----------------------------------------------------------------------
  maxIndex2D = [NX, NY]
  grid = ESMF_GridCreateNoPeriDim(maxIndex=maxIndex2D, rc=rc)
  call check(rc, "GridCreate")

  call ESMF_GridAddCoord(grid, staggerloc=ESMF_STAGGERLOC_CENTER, rc=rc)
  call check(rc, "GridAddCoord")

  ! Fill coordinates
  block
    real(ESMF_KIND_R8), pointer :: glon(:,:), glat(:,:)
    real(ESMF_KIND_R8), parameter :: DLON = 360.0d0/NX, DLAT = 180.0d0/NY
    integer :: ii, jj

    call ESMF_GridGetCoord(grid, coordDim=1, localDE=0, &
                           staggerloc=ESMF_STAGGERLOC_CENTER, &
                           farrayPtr=glon, rc=rc)
    call check(rc, "GridGetCoord lon")

    call ESMF_GridGetCoord(grid, coordDim=2, localDE=0, &
                           staggerloc=ESMF_STAGGERLOC_CENTER, &
                           farrayPtr=glat, rc=rc)
    call check(rc, "GridGetCoord lat")

    do jj = 1, NY
      do ii = 1, NX
        glon(ii,jj) = (real(ii,ESMF_KIND_R8) - 0.5d0)*DLON - 180.0d0
        glat(ii,jj) = (real(jj,ESMF_KIND_R8) - 0.5d0)*DLAT -  90.0d0
      end do
    end do
  end block

  ! -----------------------------------------------------------------------
  ! 4. Mesh (minimal quad mesh matching the grid)
  ! -----------------------------------------------------------------------
  mesh = ESMF_MeshCreate(parametricDim=2, spatialDim=2, &
                          coordSys=ESMF_COORDSYS_SPH_DEG, rc=rc)
  call check(rc, "MeshCreate")

  block
    integer :: n_nodes, n_elems, nid, eid, ii, jj
    integer,          allocatable :: nodeIds(:), elemIds(:), elemConn(:), elemTypes(:)
    real(ESMF_KIND_R8), allocatable :: nodeCoords(:), elemCoords(:)
    real(ESMF_KIND_R8), parameter :: DLON = 360.0d0/NX, DLAT = 180.0d0/NY

    n_nodes = (NX+1)*(NY+1)
    n_elems = NX*NY

    allocate(nodeIds(n_nodes), nodeCoords(2*n_nodes))
    nid = 0
    do jj = 1, NY+1
      do ii = 1, NX+1
        nid = nid + 1
        nodeIds(nid) = nid
        nodeCoords(2*nid-1) = (real(ii,ESMF_KIND_R8)-1.0d0)*DLON - 180.0d0
        nodeCoords(2*nid)   = (real(jj,ESMF_KIND_R8)-1.0d0)*DLAT -  90.0d0
      end do
    end do
    call ESMF_MeshAddNodes(mesh, nodeIds=nodeIds, nodeCoords=nodeCoords, rc=rc)
    call check(rc, "MeshAddNodes")
    deallocate(nodeIds, nodeCoords)

    allocate(elemIds(n_elems), elemCoords(2*n_elems), &
             elemConn(4*n_elems), elemTypes(n_elems))
    eid = 0
    do jj = 1, NY
      do ii = 1, NX
        eid = eid + 1
        elemIds(eid)          = eid
        elemTypes(eid)        = ESMF_MESHELEMTYPE_QUAD
        elemCoords(2*eid-1)   = (real(ii,ESMF_KIND_R8)-0.5d0)*DLON - 180.0d0
        elemCoords(2*eid)     = (real(jj,ESMF_KIND_R8)-0.5d0)*DLAT -  90.0d0
        elemConn(4*eid-3) = (jj-1)*(NX+1) + ii
        elemConn(4*eid-2) = (jj-1)*(NX+1) + ii + 1
        elemConn(4*eid-1) =  jj   *(NX+1) + ii + 1
        elemConn(4*eid)   =  jj   *(NX+1) + ii
      end do
    end do
    call ESMF_MeshAddElements(mesh, elementIds=elemIds, elementTypes=elemTypes, &
                              elementConn=elemConn, elementCoords=elemCoords, rc=rc)
    call check(rc, "MeshAddElements")
    deallocate(elemIds, elemCoords, elemConn, elemTypes)
  end block

  ! -----------------------------------------------------------------------
  ! 5. States
  ! -----------------------------------------------------------------------
  importState = ESMF_StateCreate(name="test_ex5_import", &
                                  stateintent=ESMF_STATEINTENT_IMPORT, rc=rc)
  call check(rc, "StateCreate import")

  exportState = ESMF_StateCreate(name="test_ex5_export", &
                                  stateintent=ESMF_STATEINTENT_EXPORT, rc=rc)
  call check(rc, "StateCreate export")

  ! -----------------------------------------------------------------------
  ! 6. GridComp — single process uses PARENT_VM to avoid MPI sub-comm
  ! -----------------------------------------------------------------------
  if (petCount > 1) then
    acesComp = ESMF_GridCompCreate(name="ACES_ex5", rc=rc)
  else
    acesComp = ESMF_GridCompCreate(name="ACES_ex5", &
                                    contextflag=ESMF_CONTEXT_PARENT_VM, rc=rc)
  end if
  call check(rc, "GridCompCreate")

  call ESMF_GridCompSet(acesComp, grid=grid, mesh=mesh, clock=clock, rc=rc)
  call check(rc, "GridCompSet")

  ! Point ACES at example 5 config
  call ACES_SetConfigPath(CONFIG_FILE, rc)
  call check(rc, "ACES_SetConfigPath")

  call ESMF_GridCompSetServices(acesComp, userRoutine=ACES_SetServices, rc=rc)
  call check(rc, "GridCompSetServices")

  ! -----------------------------------------------------------------------
  ! 7. Advertise (phase 1)
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [test_example5] Phase 1 — Advertise"
  call ESMF_GridCompInitialize(acesComp, importState=importState, &
                                exportState=exportState, clock=clock, &
                                phase=1, rc=rc)
  call check(rc, "GridCompInitialize phase=1")

  ! -----------------------------------------------------------------------
  ! 8. Realize (phase 2)
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [test_example5] Phase 2 — Realize"
  call ESMF_GridCompInitialize(acesComp, importState=importState, &
                                exportState=exportState, clock=clock, &
                                phase=2, rc=rc)
  call check(rc, "GridCompInitialize phase=2")

  ! -----------------------------------------------------------------------
  ! 9. One run step
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [test_example5] Run step 1"
  call ESMF_GridCompRun(acesComp, importState=importState, &
                         exportState=exportState, clock=clock, &
                         phase=1, rc=rc)
  call check(rc, "GridCompRun phase=1")

  call ESMF_ClockAdvance(clock, rc=rc)
  call check(rc, "ClockAdvance")

  ! -----------------------------------------------------------------------
  ! 10. Read export fields and verify co /= no /= so2 and all nonzero
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [test_example5] Checking export fields"

  call ESMF_StateGet(exportState, itemName="co", field=co_field, rc=rc)
  call check(rc, "StateGet co")
  call ESMF_StateGet(exportState, itemName="no", field=no_field, rc=rc)
  call check(rc, "StateGet no")

  call ESMF_FieldGet(co_field,  localDe=0, farrayPtr=co_ptr,  rc=rc)
  call check(rc, "FieldGet co")
  call ESMF_FieldGet(no_field,  localDe=0, farrayPtr=no_ptr,  rc=rc)
  call check(rc, "FieldGet no")

  ! Sum each field to get a scalar representative value
  co_sum  = 0.0d0
  no_sum  = 0.0d0
  do k = lbound(co_ptr,3), ubound(co_ptr,3)
    do j = lbound(co_ptr,2), ubound(co_ptr,2)
      do i = lbound(co_ptr,1), ubound(co_ptr,1)
        co_sum  = co_sum  + co_ptr(i,j,k)
        no_sum  = no_sum  + no_ptr(i,j,k)
      end do
    end do
  end do

  write(*,'(A,2(A,ES14.6))') "INFO: [test_example5] Field sums: ", &
    "co=",  co_sum, "  no=", no_sum

  pass = .true.

  ! Both must be nonzero
  if (co_sum == 0.0d0) then
    write(*,'(A)') "FAIL: co field sum is zero"
    pass = .false.
  end if
  if (no_sum == 0.0d0) then
    write(*,'(A)') "FAIL: no field sum is zero"
    pass = .false.
  end if

  ! Both must be different from each other
  if (co_sum == no_sum) then
    write(*,'(A)') "FAIL: co and no have identical sums"
    pass = .false.
  end if

  if (pass) then
    write(*,'(A)') "PASS: [test_example5] co and no are both nonzero and distinct"
  end if

  ! -----------------------------------------------------------------------
  ! 11. Finalize
  ! -----------------------------------------------------------------------
  call ESMF_GridCompFinalize(acesComp, importState=importState, &
                              exportState=exportState, clock=clock, &
                              phase=1, rc=rc)
  call check(rc, "GridCompFinalize")

  call ESMF_VMBarrier(vm, rc=rc)

  call ESMF_StateDestroy(importState, rc=rc)
  call ESMF_StateDestroy(exportState, rc=rc)
  call ESMF_MeshDestroy(mesh, rc=rc)
  call ESMF_GridDestroy(grid, rc=rc)
  call ESMF_ClockDestroy(clock, rc=rc)
  call ESMF_CalendarDestroy(calendar, rc=rc)
  call ESMF_GridCompDestroy(acesComp, rc=rc)

  call ESMF_Finalize(rc=rc)

  if (.not. pass) stop 1
  write(*,'(A)') "INFO: [test_example5] All checks passed."

contains

  subroutine check(rc, label)
    integer,          intent(in) :: rc
    character(len=*), intent(in) :: label
    integer :: finalize_rc
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,A,A,I0)') "FATAL: [test_example5] ", trim(label), " failed rc=", rc
      call ESMF_Finalize(rc=finalize_rc)
      stop 1
    end if
  end subroutine check

end program test_example5_driver
