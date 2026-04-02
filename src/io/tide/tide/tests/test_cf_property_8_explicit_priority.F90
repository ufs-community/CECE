!> @file test_cf_property_8_explicit_priority.F90
!> @brief Property 8: Explicit Mapping Priority
!>
!> Feature: cf-convention-auto-detection, Property 8: Explicit Mapping Priority
!> Validates: Requirement 4.2
!>
!> When both CF detection and explicit mapping are available, verify explicit
!> mapping is used (it takes priority over CF-detected mapping).
program test_cf_property_8_explicit_priority
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_priority_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 8: ', npass, '/1 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  subroutine run_priority_test(rc)
    integer, intent(out) :: rc

    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    type(cf_detection_config_t)  :: cfg
    character(len=256) :: file_var_cf, file_var_explicit
    character(len=256) :: fld_file(1), fld_model(1)
    integer :: cf_rc

    rc = 0

    ! Build a cache where 'co_emissions' has standard_name -> 'cf_file_var'
    cache%nvars = 1
    cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'
    cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'cf_file_var'
    cache%vars(1)%standard_name     = 'co_emissions'
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 0

    ! Explicit mapping: 'co_emissions' -> 'explicit_file_var'
    fld_model(1) = 'co_emissions'
    fld_file(1)  = 'explicit_file_var'

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! CF detection would find 'cf_file_var'
    call cf_match_variable('co_emissions', cache, file_var_cf, meta, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL: CF match failed unexpectedly'
      rc = 1
    end if
    if (trim(file_var_cf) /= 'cf_file_var') then
      write(*,*) 'FAIL: CF match returned wrong var: ', trim(file_var_cf)
      rc = 1
    end if

    ! Explicit mapping should return 'explicit_file_var'
    call cf_apply_explicit_mapping('co_emissions', fld_file, fld_model, 1, file_var_explicit, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL: explicit mapping failed'
      rc = 1
    end if
    if (trim(file_var_explicit) /= 'explicit_file_var') then
      write(*,*) 'FAIL: explicit mapping returned wrong var: ', trim(file_var_explicit)
      rc = 1
    end if

    ! Property: explicit result differs from CF result, confirming priority
    if (trim(file_var_explicit) == trim(file_var_cf)) then
      write(*,*) 'FAIL: explicit and CF returned same var — test setup error'
      rc = 1
    end if

    call cf_clear_cache(cache)

  end subroutine run_priority_test

end program test_cf_property_8_explicit_priority
