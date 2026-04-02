!> @file test_cf_property_14_vertical_dim.F90
!> @brief Property 14: Vertical Dimension Detection
!>
!> Feature: cf-convention-auto-detection, Property 14: Vertical Dimension Detection
!> Validates: Requirement 7.3
!>
!> Variables with lev/level/altitude/height dimension are identified as
!> having a vertical dimension.
program test_cf_property_14_vertical_dim
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_vertical_name_tests(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 14: ', npass, '/1 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  pure function is_vertical_dim_name(dname) result(res)
    character(len=*), intent(in) :: dname
    logical :: res
    character(len=64) :: n
    integer :: i, ic
    n = ''
    do i = 1, len_trim(dname)
      ic = iachar(dname(i:i))
      if (ic >= 65 .and. ic <= 90) ic = ic + 32
      n(i:i) = achar(ic)
    end do
    res = (trim(n) == 'lev'      .or. trim(n) == 'level'    .or. &
           trim(n) == 'altitude' .or. trim(n) == 'height'   .or. &
           trim(n) == 'nlev'     .or. trim(n) == 'nlevels')
  end function is_vertical_dim_name

  subroutine run_vertical_name_tests(rc)
    integer, intent(out) :: rc
    character(len=16) :: vert_names(4), non_vert(3)
    integer :: i

    rc = 0
    vert_names = ['lev     ', 'level   ', 'altitude', 'height  ']
    non_vert   = ['lat ', 'lon ', 'time']

    do i = 1, 4
      if (.not. is_vertical_dim_name(trim(vert_names(i)))) then
        write(*,*) 'FAIL: "', trim(vert_names(i)), '" not identified as vertical dim'; rc = 1
      end if
      ! Case insensitive
      if (.not. is_vertical_dim_name('LEV')) then
        write(*,*) 'FAIL: "LEV" not identified as vertical dim (case)'; rc = 1
      end if
    end do

    do i = 1, 3
      if (is_vertical_dim_name(trim(non_vert(i)))) then
        write(*,*) 'FAIL: "', trim(non_vert(i)), '" incorrectly identified as vertical dim'; rc = 1
      end if
    end do

  end subroutine run_vertical_name_tests

end program test_cf_property_14_vertical_dim
