!> @file test_cf_property_3_stdname_match.F90
!> @brief Property 3: Standard Name Matching
!>
!> Feature: cf-convention-auto-detection, Property 3: Standard Name Matching
!> Validates: Requirement 2.1
!>
!> For any model variable request with a specified standard_name, the
!> Detection_Engine SHALL search all NetCDF variables and return a match if
!> any variable has a matching standard_name attribute (after normalisation).
program test_cf_property_3_stdname_match
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, iter, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  do iter = 1, 20
    call run_one_iteration(iter, rc)
    if (rc == 0) then; npass = npass + 1
    else; nfail = nfail + 1; overall_rc = 1
    end if
  end do

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 3: ', npass, '/20 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> Build a synthetic cache with nvars variables, each having a unique
  !> standard_name of the form "std_name_N".
  subroutine build_cache(nvars, cache)
    integer,                   intent(in)  :: nvars
    type(cf_metadata_cache_t), intent(out) :: cache
    integer :: i
    cache%nvars         = nvars
    cache%is_cf_compliant = .true.
    cache%filename      = 'synthetic'
    cache%cf_version    = 'CF-1.8'
    allocate(cache%vars(nvars))
    do i = 1, nvars
      write(cache%vars(i)%var_name,      '(a,i0)') 'file_var_', i
      write(cache%vars(i)%standard_name, '(a,i0)') 'std_name_', i
      cache%vars(i)%has_standard_name = .true.
      cache%vars(i)%has_long_name     = .false.
      cache%vars(i)%has_units         = .false.
      cache%vars(i)%ndims             = 0
    end do
  end subroutine build_cache

  subroutine run_one_iteration(seed, rc)
    integer, intent(in)  :: seed
    integer, intent(out) :: rc

    integer :: nvars, target_idx, cf_rc
    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    type(cf_detection_config_t)  :: cfg
    character(len=256) :: file_var, search_name

    rc    = 0
    nvars = mod(seed, 20) + 5  ! 5..24 variables

    call build_cache(nvars, cache)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! --- Case A: search for a name that EXISTS in the cache ---
    target_idx = mod(seed, nvars) + 1
    write(search_name, '(a,i0)') 'std_name_', target_idx

    call cf_match_variable(trim(search_name), cache, file_var, meta, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL iter', seed, ': expected match for "', trim(search_name), &
                 '" but got rc=', cf_rc
      rc = 1
    end if
    write(search_name, '(a,i0)') 'file_var_', target_idx
    if (trim(file_var) /= trim(search_name)) then
      write(*,*) 'FAIL iter', seed, ': wrong file_var "', trim(file_var), &
                 '" expected "', trim(search_name), '"'
      rc = 1
    end if

    ! --- Case B: search for a name that does NOT exist ---
    search_name = 'nonexistent_standard_name_xyz'
    call cf_match_variable(trim(search_name), cache, file_var, meta, cf_rc)
    if (cf_rc == CF_SUCCESS) then
      write(*,*) 'FAIL iter', seed, ': unexpected match for nonexistent name'
      rc = 1
    end if

    call cf_clear_cache(cache)

  end subroutine run_one_iteration

end program test_cf_property_3_stdname_match
