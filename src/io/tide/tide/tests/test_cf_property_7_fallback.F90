!> @file test_cf_property_7_fallback.F90
!> @brief Property 7: Fallback to Explicit Mapping
!>
!> Feature: cf-convention-auto-detection, Property 7: Fallback to Explicit Mapping
!> Validates: Requirement 4.1
!>
!> Variables without CF attributes fall back to explicit file_var/model_var
!> mappings from YAML; verify mapping succeeds.
program test_cf_property_7_fallback
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
    'Property 7: ', npass, '/20 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  subroutine run_one_iteration(seed, rc)
    integer, intent(in)  :: seed
    integer, intent(out) :: rc

    integer :: nfields, j, cf_rc
    type(cf_detection_config_t) :: cfg
    character(len=256), allocatable :: fld_file(:), fld_model(:)
    character(len=256) :: file_var, model_var

    rc      = 0
    nfields = mod(seed, 10) + 2

    allocate(fld_file(nfields), fld_model(nfields))
    do j = 1, nfields
      write(fld_file(j),  '(a,i0)') 'file_var_', j
      write(fld_model(j), '(a,i0)') 'model_var_', j
    end do

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! Test: each model_var should resolve via explicit mapping
    do j = 1, nfields
      write(model_var, '(a,i0)') 'model_var_', j
      call cf_apply_explicit_mapping(trim(model_var), fld_file, fld_model, nfields, file_var, cf_rc)

      if (cf_rc /= CF_SUCCESS) then
        write(*,*) 'FAIL iter', seed, ' j=', j, ': explicit mapping failed rc=', cf_rc
        rc = 1; cycle
      end if

      write(model_var, '(a,i0)') 'file_var_', j
      if (trim(file_var) /= trim(model_var)) then
        write(*,*) 'FAIL iter', seed, ' j=', j, ': wrong file_var "', trim(file_var), '"'
        rc = 1
      end if
    end do

    ! Test: unknown model_var should return CF_ERR_NO_MATCH
    call cf_apply_explicit_mapping('unknown_var_xyz', fld_file, fld_model, nfields, file_var, cf_rc)
    if (cf_rc == CF_SUCCESS) then
      write(*,*) 'FAIL iter', seed, ': unexpected match for unknown_var_xyz'
      rc = 1
    end if

    deallocate(fld_file, fld_model)

  end subroutine run_one_iteration

end program test_cf_property_7_fallback
