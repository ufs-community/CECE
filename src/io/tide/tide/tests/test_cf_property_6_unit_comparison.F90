!> @file test_cf_property_6_unit_comparison.F90
!> @brief Property 6: Unit Comparison for All Mappings
!>
!> Feature: cf-convention-auto-detection, Property 6: Unit Comparison for All Mappings
!> Validates: Requirement 3.1
!>
!> For every Field_Map created, verify units comparison result is recorded
!> (identical / convertible / incompatible) and matches expected outcome.
program test_cf_property_6_unit_comparison
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_identical_units(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_convertible_units(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_incompatible_units(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_empty_units(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 6: ', npass, '/4 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run test for identical units compatibility.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_identical_units(rc)
    integer, intent(out) :: rc
    logical :: compatible, conversion_needed
    integer :: cf_rc
    type(cf_detection_config_t) :: cfg
    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_validate_units('kg m-2 s-1', 'kg m-2 s-1', compatible, conversion_needed, cf_rc)
    if (.not. compatible)        then; write(*,*) 'FAIL identical: not compatible'; rc = 1; end if
    if (conversion_needed)       then; write(*,*) 'FAIL identical: conversion_needed=T'; rc = 1; end if
    if (cf_rc /= CF_SUCCESS)     then; write(*,*) 'FAIL identical: rc=', cf_rc; rc = 1; end if

    call cf_validate_units('mol m-2 s-1', 'mol m-2 s-1', compatible, conversion_needed, cf_rc)
    if (.not. compatible)        then; write(*,*) 'FAIL identical2: not compatible'; rc = 1; end if
    if (conversion_needed)       then; write(*,*) 'FAIL identical2: conversion_needed=T'; rc = 1; end if
  end subroutine run_identical_units

  !> @brief Run test for convertible units compatibility.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_convertible_units(rc)
    integer, intent(out) :: rc
    logical :: compatible, conversion_needed
    integer :: cf_rc
    type(cf_detection_config_t) :: cfg
    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_validate_units('g m-2 s-1', 'kg m-2 s-1', compatible, conversion_needed, cf_rc)
    if (.not. compatible)        then; write(*,*) 'FAIL convertible: not compatible'; rc = 1; end if
    if (.not. conversion_needed) then; write(*,*) 'FAIL convertible: conversion_needed=F'; rc = 1; end if
    if (cf_rc /= CF_SUCCESS)     then; write(*,*) 'FAIL convertible: rc=', cf_rc; rc = 1; end if

    call cf_validate_units('K', 'degC', compatible, conversion_needed, cf_rc)
    if (.not. compatible)        then; write(*,*) 'FAIL K/degC: not compatible'; rc = 1; end if
    if (.not. conversion_needed) then; write(*,*) 'FAIL K/degC: conversion_needed=F'; rc = 1; end if
  end subroutine run_convertible_units

  !> @brief Run test for incompatible units detection.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_incompatible_units(rc)
    integer, intent(out) :: rc
    logical :: compatible, conversion_needed
    integer :: cf_rc
    type(cf_detection_config_t) :: cfg
    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_validate_units('kg', 'm/s', compatible, conversion_needed, cf_rc)
    if (compatible)              then; write(*,*) 'FAIL incompatible: compatible=T'; rc = 1; end if
    if (cf_rc /= CF_ERR_INCOMPATIBLE_UNITS) then
      write(*,*) 'FAIL incompatible: expected CF_ERR_INCOMPATIBLE_UNITS, got', cf_rc; rc = 1
    end if
  end subroutine run_incompatible_units

  !> @brief Run test for empty units compatibility.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_empty_units(rc)
    integer, intent(out) :: rc
    logical :: compatible, conversion_needed
    integer :: cf_rc
    type(cf_detection_config_t) :: cfg
    rc = 0
    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    call cf_validate_units('', 'm/s', compatible, conversion_needed, cf_rc)
    if (compatible)              then; write(*,*) 'FAIL empty: compatible=T'; rc = 1; end if
    if (cf_rc /= CF_ERR_INCOMPATIBLE_UNITS) then
      write(*,*) 'FAIL empty: expected CF_ERR_INCOMPATIBLE_UNITS, got', cf_rc; rc = 1
    end if

    call cf_validate_units('', '', compatible, conversion_needed, cf_rc)
    if (.not. compatible)        then; write(*,*) 'FAIL empty2: compatible=F'; rc = 1; end if
  end subroutine run_empty_units

end program test_cf_property_6_unit_comparison
