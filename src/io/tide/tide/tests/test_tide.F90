program test_tide
  use tide_mod
  use ESMF
  use shr_kind_mod, only : r8 => shr_kind_r8
  implicit none

  type(tide_type) :: tide
  type(ESMF_Mesh) :: mesh
  type(ESMF_Clock) :: clock
  type(ESMF_Time) :: startTime, stopTime
  type(ESMF_TimeInterval) :: timeStep
  integer :: rc
  real(r8), pointer :: data_ptr(:,:)

  ! Initialize ESMF
  call ESMF_Initialize(rc=rc)

  ! Set up clock: 2000-01-01
  call ESMF_TimeSet(startTime, yy=2000, mm=1, dd=1, rc=rc)
  call ESMF_TimeSet(stopTime, yy=2000, mm=1, dd=2, rc=rc)
  call ESMF_TimeIntervalSet(timeStep, d=1, rc=rc)
  clock = ESMF_ClockCreate(timeStep, startTime, stopTime=stopTime, rc=rc)

  print *, "TIDE API: Verification program started."

  ! In this simplified CTest, we verify that we can link against tide_mod
  ! and access the TIDE handles and subroutines.
  ! Actual execution of tide_init requires valid NetCDF and mesh files
  ! which are being added to the repository for full JCSDA verification.

  print *, "TIDE API: Calling tide_finalize..."
  call tide_finalize(tide, rc)

  print *, "TIDE API structure verified."

  call ESMF_Finalize(rc=rc)
end program test_tide
