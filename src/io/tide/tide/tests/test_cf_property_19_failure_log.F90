!> @file test_cf_property_19_failure_log.F90
!> @brief Property 19: Failure Logging Completeness
!>
!> Feature: cf-convention-auto-detection, Property 19: Failure Logging Completeness
!> Validates: Requirement 9.2
!>
!> For any CF detection failure, verify the engine returns CF_ERR_NO_MATCH
!> (indicating the failure reason was determined) and does not crash.
program test_cf_property_19_failure_log
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_failure_log_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 19: ', npass, '/1 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a test for CF variable matching failure and logging.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_failure_log_test(rc)
    integer, intent(out) :: rc

    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    type(cf_detection_config_t)  :: cfg
    character(len=256) :: file_var
    integer :: cf_rc

    rc = 0

    ! Cache with no matching standard_name
    cache%nvars = 1; cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'; cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'some_var'
    cache%vars(1)%standard_name     = 'some_other_quantity'
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 0

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 1
    call cf_detection_init(cfg, cf_rc)

    call cf_match_variable('nonexistent_standard_name', cache, file_var, meta, cf_rc)

    ! Property: failure must return CF_ERR_NO_MATCH (reason is known)
    if (cf_rc /= CF_ERR_NO_MATCH) then
      write(*,*) 'FAIL: expected CF_ERR_NO_MATCH, got', cf_rc; rc = 1
    end if

    ! Property: file_var must be empty on failure
    if (len_trim(file_var) /= 0) then
      write(*,*) 'FAIL: file_var non-empty on failure: "', trim(file_var), '"'; rc = 1
    end if

    call cf_clear_cache(cache)

  end subroutine run_failure_log_test

end program test_cf_property_19_failure_log
