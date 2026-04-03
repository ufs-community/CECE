!> @file test_cf_property_2_missing_attrs.F90
!> @brief Property 2: Graceful Handling of Missing Attributes
!>
!> Feature: cf-convention-auto-detection, Property 2: Graceful Handling of Missing Attributes
!> Validates: Requirement 1.5
!>
!> For any variable lacking CF convention attributes, the Detection_Engine SHALL
!> log a warning, continue processing other variables, and not terminate.
program test_cf_property_2_missing_attrs
  use tide_cf_detection_mod
  use ESMF
  use pio
  use netcdf
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
    'Property 2: ', npass, '/20 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a single test iteration for missing CF attributes.
  !> @param seed Random seed for test variation
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_one_iteration(seed, rc)
    integer, intent(in)  :: seed
    integer, intent(out) :: rc

    integer :: nvars, ivar, ncid, varid, dimid, cf_rc
    integer :: n_with_attrs, n_without_attrs
    character(len=64) :: fname, vname
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    integer :: my_task, n_tasks, comm
    type(ESMF_VM) :: vm
    logical :: add_attrs

    rc    = 0
    nvars = 10

    write(fname, '(a,i0,a)') '/tmp/cf_prop2_test_', seed, '.nc'
    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if

    cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
    cf_rc = nf90_def_dim(ncid, 'x', 5, dimid)

    n_with_attrs    = 0
    n_without_attrs = 0

    do ivar = 1, nvars
      write(vname, '(a,i0)') 'var_', ivar
      cf_rc = nf90_def_var(ncid, trim(vname), NF90_FLOAT, [dimid], varid)
      ! Omit CF attributes from ~50% of variables (odd-numbered in this seed)
      add_attrs = (mod(ivar + seed, 2) == 0)
      if (add_attrs) then
        cf_rc = nf90_put_att(ncid, varid, 'standard_name', 'std_'//trim(vname))
        cf_rc = nf90_put_att(ncid, varid, 'long_name',     'Long '//trim(vname))
        cf_rc = nf90_put_att(ncid, varid, 'units',         'kg m-2 s-1')
        n_with_attrs = n_with_attrs + 1
      else
        n_without_attrs = n_without_attrs + 1
      end if
    end do

    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! Property: must NOT abort even when attributes are missing
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL iter', seed, ': cf_read_file_metadata aborted with rc=', cf_rc
      rc = 1
    end if

    ! Property: all variables must still be present in cache
    if (cache%nvars /= nvars) then
      write(*,*) 'FAIL iter', seed, ': expected nvars=', nvars, ' got', cache%nvars
      rc = 1
    end if

    ! Property: variables with attrs have flags set; those without do not
    do ivar = 1, cache%nvars
      add_attrs = (mod(ivar + seed, 2) == 0)
      if (add_attrs) then
        if (.not. cache%vars(ivar)%has_standard_name) then
          write(*,*) 'FAIL iter', seed, ': var', ivar, 'should have standard_name'
          rc = 1
        end if
      else
        if (cache%vars(ivar)%has_standard_name) then
          write(*,*) 'FAIL iter', seed, ': var', ivar, 'should NOT have standard_name'
          rc = 1
        end if
      end if
    end do

    call cf_clear_cache(cache)
    call PIO_Finalize(pio_sys, cf_rc)
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_one_iteration

end program test_cf_property_2_missing_attrs
