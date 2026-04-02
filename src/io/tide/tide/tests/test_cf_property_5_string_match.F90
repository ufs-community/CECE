!> @file test_cf_property_5_string_match.F90
!> @brief Property 5: Robust String Matching
!>
!> Feature: cf-convention-auto-detection, Property 5: Robust String Matching
!> Validates: Requirements 2.4, 2.5
!>
!> For any standard_name comparison, the Detection_Engine SHALL perform
!> case-insensitive matching and normalise whitespace before comparison,
!> ensuring that "air_temperature", "Air_Temperature", and "air_temperature "
!> all match.
program test_cf_property_5_string_match
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_case_tests(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_whitespace_tests(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_mixed_tests(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_tab_tests(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 5: ', npass, '/4 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> Build a cache with a single variable whose standard_name is canonical_name.
  subroutine build_single_var_cache(canonical_name, cache)
    character(len=*),          intent(in)  :: canonical_name
    type(cf_metadata_cache_t), intent(out) :: cache
    cache%nvars = 1
    cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'
    cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'the_var'
    cache%vars(1)%standard_name     = trim(canonical_name)
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 0
  end subroutine build_single_var_cache

  !> Verify that search_name matches the cache (or not, per expect_match).
  subroutine assert_match(label, cache, search_name, expect_match, rc)
    character(len=*),          intent(in)    :: label, search_name
    type(cf_metadata_cache_t), intent(in)    :: cache
    logical,                   intent(in)    :: expect_match
    integer,                   intent(inout) :: rc

    type(cf_variable_metadata_t) :: meta
    character(len=256) :: file_var
    integer :: cf_rc

    call cf_match_variable(trim(search_name), cache, file_var, meta, cf_rc)

    if (expect_match .and. cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL [', trim(label), ']: expected match for "', &
                 trim(search_name), '" but got rc=', cf_rc
      rc = 1
    else if (.not. expect_match .and. cf_rc == CF_SUCCESS) then
      write(*,*) 'FAIL [', trim(label), ']: unexpected match for "', trim(search_name), '"'
      rc = 1
    end if
  end subroutine assert_match

  ! Test case-insensitive matching
  subroutine run_case_tests(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    integer :: cf_rc

    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call build_single_var_cache('air_temperature', cache)

    call assert_match('lowercase',  cache, 'air_temperature',  .true.,  rc)
    call assert_match('UPPERCASE',  cache, 'AIR_TEMPERATURE',  .true.,  rc)
    call assert_match('MixedCase',  cache, 'Air_Temperature',  .true.,  rc)
    call assert_match('MixedCase2', cache, 'aIr_tEmPeRaTuRe',  .true.,  rc)
    call assert_match('different',  cache, 'sea_surface_temp', .false., rc)

    call cf_clear_cache(cache)
  end subroutine run_case_tests

  ! Test whitespace normalisation
  subroutine run_whitespace_tests(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    integer :: cf_rc

    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call build_single_var_cache('air_temperature', cache)

    call assert_match('trailing_space',  cache, 'air_temperature ',   .true., rc)
    call assert_match('leading_space',   cache, ' air_temperature',   .true., rc)
    call assert_match('both_spaces',     cache, ' air_temperature ',  .true., rc)

    call cf_clear_cache(cache)
  end subroutine run_whitespace_tests

  ! Test mixed case + whitespace combinations
  subroutine run_mixed_tests(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    integer :: cf_rc

    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call build_single_var_cache('tendency_of_atmosphere_mass_content_of_carbon_monoxide', cache)

    call assert_match('exact',       cache, &
      'tendency_of_atmosphere_mass_content_of_carbon_monoxide', .true., rc)
    call assert_match('upper',       cache, &
      'TENDENCY_OF_ATMOSPHERE_MASS_CONTENT_OF_CARBON_MONOXIDE', .true., rc)
    call assert_match('trailing',    cache, &
      'tendency_of_atmosphere_mass_content_of_carbon_monoxide ', .true., rc)
    call assert_match('wrong_name',  cache, &
      'tendency_of_atmosphere_mass_content_of_sulfur_dioxide',  .false., rc)

    call cf_clear_cache(cache)
  end subroutine run_mixed_tests

  ! Test tabs and multiple spaces
  subroutine run_tab_tests(rc)
    integer, intent(out) :: rc
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    integer :: cf_rc

    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call build_single_var_cache('air temperature', cache)

    call assert_match('tab',             cache, 'air'//achar(9)//'temperature',  .true., rc)
    call assert_match('multiple_spaces', cache, 'air    temperature',  .true., rc)
    call assert_match('tabs_and_spaces', cache, 'air '//achar(9)//'   temperature',  .true., rc)

    call cf_clear_cache(cache)
  end subroutine run_tab_tests

end program test_cf_property_5_string_match
