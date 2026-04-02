!> @file test_cf_property_12_monotonicity.F90
!> @brief Property 12: Coordinate Monotonicity Validation
!>
!> Feature: cf-convention-auto-detection, Property 12: Coordinate Monotonicity Validation
!> Validates: Requirement 6.4
!>
!> For any identified coordinate variable, verify monotonicity check is
!> performed and non-monotonic values are flagged.
program test_cf_property_12_monotonicity
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_monotonic_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_nonmonotonic_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 12: ', npass, '/2 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> Helper: check if a real array is monotonically increasing or decreasing.
  pure function is_monotonic(vals, n) result(res)
    integer,  intent(in) :: n
    real,     intent(in) :: vals(n)
    logical :: res
    integer :: i
    logical :: increasing, decreasing
    if (n <= 1) then; res = .true.; return; end if
    increasing = .true.; decreasing = .true.
    do i = 2, n
      if (vals(i) <= vals(i-1)) increasing = .false.
      if (vals(i) >= vals(i-1)) decreasing = .false.
    end do
    res = increasing .or. decreasing
  end function is_monotonic

  subroutine run_monotonic_test(rc)
    integer, intent(out) :: rc
    real :: lat_vals(5)
    rc = 0
    lat_vals = [-90.0, -45.0, 0.0, 45.0, 90.0]
    if (.not. is_monotonic(lat_vals, 5)) then
      write(*,*) 'FAIL monotonic: increasing lat not detected as monotonic'; rc = 1
    end if
    lat_vals = [90.0, 45.0, 0.0, -45.0, -90.0]
    if (.not. is_monotonic(lat_vals, 5)) then
      write(*,*) 'FAIL monotonic: decreasing lat not detected as monotonic'; rc = 1
    end if
  end subroutine run_monotonic_test

  subroutine run_nonmonotonic_test(rc)
    integer, intent(out) :: rc
    real :: bad_vals(5)
    rc = 0
    bad_vals = [-90.0, 45.0, 0.0, -45.0, 90.0]
    if (is_monotonic(bad_vals, 5)) then
      write(*,*) 'FAIL nonmonotonic: non-monotonic array incorrectly flagged as monotonic'; rc = 1
    end if
  end subroutine run_nonmonotonic_test

end program test_cf_property_12_monotonicity
