!> @file test_cf_property_18_success_log.F90
!> @brief Property 18: Success Logging Completeness
!>
!> Feature: cf-convention-auto-detection, Property 18: Success Logging Completeness
!> Validates: Requirement 9.1
!>
!> For any successful CF detection, verify the log contains standard_name,
!> file_var, model_var, and units.  We verify this by checking that
!> cf_match_variable succeeds and the returned metadata carries all fields.
program test_cf_property_18_success_log
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_success_log_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 18: ', npass, '/1 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  subroutine run_success_log_test(rc)
    integer, intent(out) :: rc

    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    type(cf_detection_config_t)  :: cfg
    character(len=256) :: file_var
    integer :: cf_rc

    rc = 0

    ! Build cache with full CF metadata
    cache%nvars = 1; cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'; cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'CO'
    cache%vars(1)%standard_name     = 'tendency_of_atmosphere_mass_content_of_carbon_monoxide'
    cache%vars(1)%long_name         = 'CO surface emissions'
    cache%vars(1)%units             = 'kg m-2 s-1'
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .true.
    cache%vars(1)%has_units         = .true.
    cache%vars(1)%ndims             = 2

    ! Enable info-level logging so success messages are emitted
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 2
    call cf_detection_init(cfg, cf_rc)

    call cf_match_variable( &
      'tendency_of_atmosphere_mass_content_of_carbon_monoxide', &
      cache, file_var, meta, cf_rc)

    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL: match failed rc=', cf_rc; rc = 1
    end if

    ! Property: returned metadata must carry all log-relevant fields
    if (len_trim(meta%standard_name) == 0) then
      write(*,*) 'FAIL: meta%standard_name empty after successful match'; rc = 1
    end if
    if (len_trim(file_var) == 0) then
      write(*,*) 'FAIL: file_var empty after successful match'; rc = 1
    end if
    if (.not. meta%has_units) then
      write(*,*) 'FAIL: meta%has_units false after successful match'; rc = 1
    end if
    if (len_trim(meta%units) == 0) then
      write(*,*) 'FAIL: meta%units empty after successful match'; rc = 1
    end if

    call cf_clear_cache(cache)

  end subroutine run_success_log_test

end program test_cf_property_18_success_log
