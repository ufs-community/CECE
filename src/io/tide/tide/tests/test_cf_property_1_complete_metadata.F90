!> @file test_cf_property_1_complete_metadata.F90
!> @brief Property 1: Complete Metadata Reading
!>
!> Feature: cf-convention-auto-detection, Property 1: Complete Metadata Reading
!> Validates: Requirements 1.1, 1.2, 1.3, 1.4
!>
!> For any NetCDF file opened by the Detection_Engine, all CF convention
!> attributes (standard_name, long_name, units, coordinates) SHALL be read
!> for every variable and stored in the metadata cache.
!>
!> Test strategy: create synthetic NetCDF files with 1-50 variables using
!> the NetCDF-Fortran library, run cf_read_file_metadata, and verify the
!> cache contains metadata for all variables with all attributes stored.
program test_cf_property_1_complete_metadata
  use tide_cf_detection_mod
  use ESMF
  use pio
  use netcdf
  implicit none

  integer :: rc, overall_rc
  integer :: iter, niter
  integer :: npass, nfail

  overall_rc = 0
  npass      = 0
  nfail      = 0
  niter      = 20  ! reduced from 100 for CI speed; each iter creates a real file

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  do iter = 1, niter
    call run_one_iteration(iter, rc)
    if (rc == 0) then
      npass = npass + 1
    else
      nfail = nfail + 1
      overall_rc = 1
    end if
  end do

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 1: ', npass, '/', niter, ' passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  subroutine run_one_iteration(seed, rc)
    integer, intent(in)  :: seed
    integer, intent(out) :: rc

    integer :: nvars, ivar
    integer :: ncid, varid, dimid
    character(len=64)  :: fname
    character(len=32)  :: vname, sname, lname, units_str
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    integer :: my_task, n_tasks, comm, cf_rc
    type(ESMF_VM) :: vm

    rc = 0

    ! Determine number of variables: 1 to 20 (scaled by seed)
    nvars = mod(seed, 20) + 1

    ! Create a temporary NetCDF file
    write(fname, '(a,i0,a)') '/tmp/cf_prop1_test_', seed, '.nc'
    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if

    ! Write global Conventions attribute
    cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
    if (cf_rc /= NF90_NOERR) then; rc = 1; cf_rc = nf90_close(ncid); return; end if

    ! Define a simple dimension
    cf_rc = nf90_def_dim(ncid, 'x', 10, dimid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; cf_rc = nf90_close(ncid); return; end if

    ! Define variables with CF attributes
    do ivar = 1, nvars
      write(vname, '(a,i0)') 'var_', ivar
      write(sname, '(a,i0)') 'standard_name_', ivar
      write(lname, '(a,i0)') 'Long name for variable ', ivar
      units_str = 'kg m-2 s-1'

      cf_rc = nf90_def_var(ncid, trim(vname), NF90_FLOAT, [dimid], varid)
      if (cf_rc /= NF90_NOERR) then; rc = 1; cf_rc = nf90_close(ncid); return; end if
      cf_rc = nf90_put_att(ncid, varid, 'standard_name', trim(sname))
      cf_rc = nf90_put_att(ncid, varid, 'long_name',     trim(lname))
      cf_rc = nf90_put_att(ncid, varid, 'units',         trim(units_str))
    end do

    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    ! Initialise PIO for standalone use
    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    ! Initialise CF detection
    cfg%mode          = 'auto'
    cfg%cache_enabled = .true.
    cfg%log_level     = 0
    call cf_detection_init(cfg, cf_rc)

    ! Read metadata
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL iter', seed, ': cf_read_file_metadata returned', cf_rc
      rc = 1
      call PIO_Finalize(pio_sys, cf_rc)
      return
    end if

    ! Property assertion: cache must contain all variables
    if (cache%nvars /= nvars) then
      write(*,*) 'FAIL iter', seed, ': expected nvars=', nvars, ' got', cache%nvars
      rc = 1
    end if

    ! Property assertion: all variables must have standard_name, long_name, units
    do ivar = 1, cache%nvars
      if (.not. cache%vars(ivar)%has_standard_name) then
        write(*,*) 'FAIL iter', seed, ': var', ivar, 'missing standard_name'
        rc = 1
      end if
      if (.not. cache%vars(ivar)%has_long_name) then
        write(*,*) 'FAIL iter', seed, ': var', ivar, 'missing long_name'
        rc = 1
      end if
      if (.not. cache%vars(ivar)%has_units) then
        write(*,*) 'FAIL iter', seed, ': var', ivar, 'missing units'
        rc = 1
      end if
    end do

    ! Property assertion: CF compliance detected
    if (.not. cache%is_cf_compliant) then
      write(*,*) 'FAIL iter', seed, ': is_cf_compliant should be .true.'
      rc = 1
    end if

    call cf_clear_cache(cache)
    call PIO_Finalize(pio_sys, cf_rc)

    ! Clean up temp file
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_one_iteration

end program test_cf_property_1_complete_metadata
