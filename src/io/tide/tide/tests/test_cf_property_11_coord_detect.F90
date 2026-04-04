!> @file test_cf_property_11_coord_detect.F90
!> @brief Property 11: Coordinate Variable Identification
!>
!> Feature: cf-convention-auto-detection, Property 11: Coordinate Variable Identification
!> Validates: Requirements 6.1, 6.2, 6.3
!>
!> Files with variables named lat/latitude/lon/longitude/time/lev/level are
!> all identified as coordinate variables.
program test_cf_property_11_coord_detect
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_standard_names_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_standard_name_attr_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_no_coords_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_mixed_coord_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 11: ', npass, '/4 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Build a CF metadata cache with given variable names.
  !> @param var_names Array of variable names
  !> @param n Number of variables
  !> @param cache Output metadata cache
  subroutine build_cache_with_vars(var_names, n, cache)
    integer,          intent(in)  :: n
    character(len=*), intent(in)  :: var_names(n)
    type(cf_metadata_cache_t), intent(out) :: cache
    integer :: i
    cache%nvars = n; cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'; cache%cf_version = 'CF-1.8'
    allocate(cache%vars(n))
    do i = 1, n
      cache%vars(i)%var_name          = trim(var_names(i))
      cache%vars(i)%standard_name     = ''
      cache%vars(i)%has_standard_name = .false.
      cache%vars(i)%has_long_name     = .false.
      cache%vars(i)%has_units         = .false.
      cache%vars(i)%ndims             = 1
    end do
  end subroutine build_cache_with_vars

  !> @brief Run test for standard coordinate variable names.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_standard_names_test(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_coordinate_info_t), allocatable :: coords(:)
    type(cf_detection_config_t) :: cfg
    character(len=32) :: vnames(7)
    integer :: cf_rc

    rc = 0
    vnames = ['lat      ', 'latitude ', 'lon      ', 'longitude', &
              'time     ', 'lev      ', 'level    ']

    call build_cache_with_vars(vnames, 7, cache)
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_detect_coordinates(cache, coords, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL standard names: detect returned rc=', cf_rc; rc = 1
    end if
    if (.not. allocated(coords)) then
      write(*,*) 'FAIL standard names: coords not allocated'; rc = 1
    else if (size(coords) /= 7) then
      write(*,*) 'FAIL standard names: expected 7 coords, got', size(coords); rc = 1
    end if

    call cf_clear_cache(cache)
    if (allocated(coords)) deallocate(coords)
  end subroutine run_standard_names_test

  !> @brief Run test for standard_name attribute detection.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_standard_name_attr_test(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_coordinate_info_t), allocatable :: coords(:)
    type(cf_detection_config_t) :: cfg
    integer :: cf_rc

    rc = 0
    ! Variable named 'x' but with standard_name='latitude'
    cache%nvars = 1; cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'; cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'x'
    cache%vars(1)%standard_name     = 'latitude'
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 1

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_detect_coordinates(cache, coords, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL std_name attr: detect returned rc=', cf_rc; rc = 1
    end if
    if (.not. allocated(coords) .or. size(coords) /= 1) then
      write(*,*) 'FAIL std_name attr: expected 1 coord'; rc = 1
    end if

    call cf_clear_cache(cache)
    if (allocated(coords)) deallocate(coords)
  end subroutine run_standard_name_attr_test

  !> @brief Run test for missing coordinate variables.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_no_coords_test(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_coordinate_info_t), allocatable :: coords(:)
    type(cf_detection_config_t) :: cfg
    character(len=32) :: vnames(2)
    integer :: cf_rc

    rc = 0
    vnames = ['emission_co ', 'emission_nox']
    call build_cache_with_vars(vnames, 2, cache)
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_detect_coordinates(cache, coords, cf_rc)
    if (cf_rc /= CF_ERR_MISSING_COORDINATES) then
      write(*,*) 'FAIL no coords: expected CF_ERR_MISSING_COORDINATES, got', cf_rc; rc = 1
    end if

    call cf_clear_cache(cache)
    if (allocated(coords)) deallocate(coords)
  end subroutine run_no_coords_test

  !> @brief Run test for mixed coordinate variable and attribute.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_mixed_coord_test(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_coordinate_info_t), allocatable :: coords(:)
    type(cf_detection_config_t) :: cfg
    integer :: cf_rc

    rc = 0
    ! Variable named 'emission_co' but with standard_name='longitude'
    cache%nvars = 1; cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'; cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'emission_co'
    cache%vars(1)%standard_name     = 'longitude'
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 1

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_detect_coordinates(cache, coords, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL mixed coord: detect returned rc=', cf_rc; rc = 1
    end if
    if (.not. allocated(coords) .or. size(coords) /= 1) then
      write(*,*) 'FAIL mixed coord: expected 1 coord'; rc = 1
    end if

    call cf_clear_cache(cache)
    if (allocated(coords)) deallocate(coords)
  end subroutine run_mixed_coord_test

end program test_cf_property_11_coord_detect
