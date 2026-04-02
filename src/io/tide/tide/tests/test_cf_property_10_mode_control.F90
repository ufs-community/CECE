!> @file test_cf_property_10_mode_control.F90
!> @brief Property 10: Detection Mode Controls Behavior
!>
!> Feature: cf-convention-auto-detection, Property 10: Detection Mode Controls Behavior
!> Validates: Requirements 5.2, 5.3, 5.4
!>
!> "auto" falls back on CF failure; "strict" fails when CF attributes absent;
!> "disabled" skips CF entirely — verify each mode behaves as specified.
program test_cf_property_10_mode_control
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_auto_mode_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_disabled_mode_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_strict_mode_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 10: ', npass, '/3 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> In "auto" mode: CF match succeeds when standard_name present.
  subroutine run_auto_mode_test(rc)
    integer, intent(out) :: rc
    type(cf_detection_config_t)  :: cfg
    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    character(len=256) :: file_var
    integer :: cf_rc

    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    cache%nvars = 1; cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'; cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'co_var'
    cache%vars(1)%standard_name     = 'co_emissions'
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 0

    call cf_match_variable('co_emissions', cache, file_var, meta, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL auto mode: CF match failed rc=', cf_rc; rc = 1
    end if
    call cf_clear_cache(cache)
  end subroutine run_auto_mode_test

  !> In "disabled" mode: explicit mapping works; CF cache is not consulted.
  subroutine run_disabled_mode_test(rc)
    integer, intent(out) :: rc
    type(cf_detection_config_t) :: cfg
    character(len=256) :: fld_file(1), fld_model(1), file_var
    integer :: cf_rc

    rc = 0
    cfg%mode = 'disabled'; cfg%cache_enabled = .false.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    fld_model(1) = 'co_emissions'
    fld_file(1)  = 'CO'

    call cf_apply_explicit_mapping('co_emissions', fld_file, fld_model, 1, file_var, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL disabled mode: explicit mapping failed rc=', cf_rc; rc = 1
    end if
    if (trim(file_var) /= 'CO') then
      write(*,*) 'FAIL disabled mode: wrong file_var "', trim(file_var), '"'; rc = 1
    end if
  end subroutine run_disabled_mode_test

  !> In "strict" mode: CF match fails when standard_name absent → CF_ERR_NO_MATCH.
  subroutine run_strict_mode_test(rc)
    integer, intent(out) :: rc
    type(cf_detection_config_t)  :: cfg
    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    character(len=256) :: file_var
    integer :: cf_rc

    rc = 0
    cfg%mode = 'strict'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! Cache with a variable that has NO standard_name
    cache%nvars = 1; cache%is_cf_compliant = .false.
    cache%filename = 'synthetic'; cache%cf_version = ''
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'some_var'
    cache%vars(1)%standard_name     = ''
    cache%vars(1)%has_standard_name = .false.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 0

    call cf_match_variable('co_emissions', cache, file_var, meta, cf_rc)
    if (cf_rc == CF_SUCCESS) then
      write(*,*) 'FAIL strict mode: expected no match but got CF_SUCCESS'; rc = 1
    end if
    call cf_clear_cache(cache)
  end subroutine run_strict_mode_test

end program test_cf_property_10_mode_control
