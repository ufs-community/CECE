!> @file test_cf_property_16_caching.F90
!> @brief Property 16: Metadata Caching
!>
!> Feature: cf-convention-auto-detection, Property 16: Metadata Caching
!> Validates: Requirements 8.1, 8.2
!>
!> For any NetCDF file, when first opened all CF attributes SHALL be cached,
!> and subsequent variable lookups SHALL reuse the cached metadata.
program test_cf_property_16_caching
  use tide_cf_detection_mod
  use ESMF
  use pio
  use netcdf
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_cache_consistency_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 16: ', npass, '/1 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a test for CF metadata cache consistency and repeated lookups.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_cache_consistency_test(rc)
    integer, intent(out) :: rc

    integer :: ncid, varid, dimid, cf_rc, lookup, my_task, n_tasks, comm
    character(len=64) :: fname
    type(cf_metadata_cache_t) :: cache1, cache2
    type(cf_variable_metadata_t) :: meta
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    type(ESMF_VM) :: vm
    character(len=256) :: file_var
    integer :: n_lookups, n_hits

    rc = 0
    fname = '/tmp/cf_prop16_cache_test.nc'

    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if
    cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
    cf_rc = nf90_def_dim(ncid, 'x', 10, dimid)
    cf_rc = nf90_def_var(ncid, 'co_var', NF90_FLOAT, [dimid], varid)
    cf_rc = nf90_put_att(ncid, varid, 'standard_name', 'co_emissions')
    cf_rc = nf90_put_att(ncid, varid, 'units', 'kg m-2 s-1')
    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! First read — populates cache
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache1, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL: first read failed rc=', cf_rc; rc = 1
    end if

    ! Store cache (simulates caching)
    call cf_cache_metadata(cache1, cf_rc)

    ! Perform multiple lookups against the same cache — all should succeed
    n_lookups = 10
    n_hits     = 0
    do lookup = 1, n_lookups
      call cf_match_variable('co_emissions', cache1, file_var, meta, cf_rc)
      if (cf_rc == CF_SUCCESS) n_hits = n_hits + 1
    end do

    if (n_hits /= n_lookups) then
      write(*,*) 'FAIL: cache hit rate', n_hits, '/', n_lookups, '< 100%'; rc = 1
    end if

    ! Second read of same file — metadata must be consistent
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache2, cf_rc)
    if (cache1%nvars /= cache2%nvars) then
      write(*,*) 'FAIL: inconsistent nvars between reads:', cache1%nvars, 'vs', cache2%nvars
      rc = 1
    end if

    call cf_clear_cache(cache1)
    call cf_clear_cache(cache2)
    call PIO_Finalize(pio_sys, cf_rc)
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_cache_consistency_test

end program test_cf_property_16_caching
