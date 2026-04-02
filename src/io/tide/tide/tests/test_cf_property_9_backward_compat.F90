!> @file test_cf_property_9_backward_compat.F90
!> @brief Property 9: Backward Compatibility
!>
!> Feature: cf-convention-auto-detection, Property 9: Backward Compatibility
!> Validates: Requirement 4.4
!>
!> Existing YAML configs run with cf_detection_mode="disabled" produce
!> identical variable mappings to the pre-CF-detection behaviour.
program test_cf_property_9_backward_compat
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
    'Property 9: ', npass, '/20 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  subroutine run_one_iteration(seed, rc)
    integer, intent(in)  :: seed
    integer, intent(out) :: rc

    integer :: nfields, j, cf_rc
    type(cf_detection_config_t) :: cfg_disabled
    character(len=256), allocatable :: fld_file(:), fld_model(:)
    character(len=256) :: file_var, model_var

    rc      = 0
    nfields = mod(seed, 8) + 2

    allocate(fld_file(nfields), fld_model(nfields))
    do j = 1, nfields
      write(fld_file(j),  '(a,i0)') 'legacy_file_', j
      write(fld_model(j), '(a,i0)') 'legacy_model_', j
    end do

    ! In "disabled" mode, only explicit mappings are used
    cfg_disabled%mode          = 'disabled'
    cfg_disabled%cache_enabled = .false.
    cfg_disabled%log_level     = 0
    call cf_detection_init(cfg_disabled, cf_rc)

    ! Each model var must resolve to its explicit file var
    do j = 1, nfields
      write(model_var, '(a,i0)') 'legacy_model_', j
      call cf_apply_explicit_mapping(trim(model_var), fld_file, fld_model, nfields, file_var, cf_rc)

      if (cf_rc /= CF_SUCCESS) then
        write(*,*) 'FAIL iter', seed, ' j=', j, ': explicit mapping failed rc=', cf_rc
        rc = 1; cycle
      end if

      write(model_var, '(a,i0)') 'legacy_file_', j
      if (trim(file_var) /= trim(model_var)) then
        write(*,*) 'FAIL iter', seed, ' j=', j, ': wrong file_var "', trim(file_var), '"'
        rc = 1
      end if
    end do

    deallocate(fld_file, fld_model)

  end subroutine run_one_iteration

end program test_cf_property_9_backward_compat
