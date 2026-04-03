!> @file test_example5_driver.F90
!> @brief Fortran NUOPC integration test: run aces_config_ex5.yaml through the
!>        NUOPC single-model app architecture and verify that co and no export
!>        fields are all nonzero and mutually distinct.
!>
!> Pass/fail is communicated via process exit code (0 = pass, 1 = fail).

program test_example5_driver
  use ESMF
  use NUOPC
  use driver, only: driver_SS => SetServices, set_driver_config_file, set_aces_config_file
  implicit none

  ! -----------------------------------------------------------------------
  ! ESMF handles
  ! -----------------------------------------------------------------------
  type(ESMF_GridComp)     :: drvComp
  type(ESMF_GridComp), pointer :: compList(:)
  type(ESMF_State)        :: exportState
  type(ESMF_Field)        :: co_field, no_field

  ! -----------------------------------------------------------------------
  ! Runtime variables
  ! -----------------------------------------------------------------------
  integer :: rc, userRc, compCount
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

  write(*,'(A)') "INFO: [test_example5] ESMF initialized"

  ! -----------------------------------------------------------------------
  ! 2. Set Configuration Files
  ! -----------------------------------------------------------------------
  ! Note: We use a dummy non-existent file or rely on driver defaults for driver.cfg
  ! as the YAML timing takes precedence, and the Grid is built internally in Example 5.
  call set_driver_config_file("aces_driver.cfg")
  call set_aces_config_file("examples/aces_config_ex5.yaml")

  ! -----------------------------------------------------------------------
  ! 3. Create Driver Component
  ! -----------------------------------------------------------------------
  drvComp = ESMF_GridCompCreate(name="driver", rc=rc)
  call check(rc, "ESMF_GridCompCreate")

  ! -----------------------------------------------------------------------
  ! 4. Set Driver Services
  ! -----------------------------------------------------------------------
  call ESMF_GridCompSetServices(drvComp, driver_SS, userRc=userRc, rc=rc)
  call check(rc, "ESMF_GridCompSetServices rc")
  call check(userRc, "ESMF_GridCompSetServices userRc")

  ! -----------------------------------------------------------------------
  ! 5. Initialize Driver
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [test_example5] Calling ESMF_GridCompInitialize..."
  call ESMF_GridCompInitialize(drvComp, userRc=userRc, rc=rc)
  call check(rc, "ESMF_GridCompInitialize rc")
  call check(userRc, "ESMF_GridCompInitialize userRc")

  ! -----------------------------------------------------------------------
  ! 6. Run Driver
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [test_example5] Calling ESMF_GridCompRun..."
  call ESMF_GridCompRun(drvComp, userRc=userRc, rc=rc)
  call check(rc, "ESMF_GridCompRun rc")
  call check(userRc, "ESMF_GridCompRun userRc")

  ! -----------------------------------------------------------------------
  ! 7. Read export fields from ACES child component
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [test_example5] Checking export fields"

  ! Retrieve the child components
  call NUOPC_DriverGet(drvComp, compList=compList, rc=rc)
  call check(rc, "NUOPC_DriverGet")

  ! Get the export state from the ACES component (it should be the first and only child)
  call ESMF_GridCompGet(compList(1), exportState=exportState, rc=rc)
  call check(rc, "ESMF_GridCompGet exportState")

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
  ! 8. Finalize
  ! -----------------------------------------------------------------------
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
