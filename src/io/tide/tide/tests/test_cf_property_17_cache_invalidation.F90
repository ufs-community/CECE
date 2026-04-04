!> @file test_cf_property_17_cache_invalidation.F90
!> @brief Property 17: Cache Invalidation on Reopen
!>
!> Feature: cf-convention-auto-detection, Property 17: Cache Invalidation on Reopen
!> Validates: Requirement 8.3
!>
!> Close and reopen a file after modifying attributes; verify cache is
!> invalidated and new metadata reflects the changes.
program test_cf_property_17_cache_invalidation
  use tide_cf_detection_mod
  use ESMF
  use pio
  use netcdf
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_invalidation_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 17: ', npass, '/1 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a test for CF metadata cache invalidation and update.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_invalidation_test(rc)
    integer, intent(out) :: rc

    integer :: ncid, varid, dimid, cf_rc, my_task, n_tasks, comm
    character(len=64) :: fname
    type(cf_metadata_cache_t) :: cache_v1, cache_v2
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    type(ESMF_VM) :: vm

    rc = 0
    fname = '/tmp/cf_prop17_invalidation.nc'

    ! Create file v1: standard_name = 'co_emissions'
    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if
    cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
    cf_rc = nf90_def_dim(ncid, 'x', 5, dimid)
    cf_rc = nf90_def_var(ncid, 'myvar', NF90_FLOAT, [dimid], varid)
    cf_rc = nf90_put_att(ncid, varid, 'standard_name', 'co_emissions')
    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! Read v1
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache_v1, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL: v1 read failed'; rc = 1
    end if

    ! Clear cache (simulates close + invalidation)
    call cf_clear_cache(cache_v1)
    if (cache_v1%nvars /= 0) then
      write(*,*) 'FAIL: cache not cleared after cf_clear_cache'; rc = 1
    end if

    ! Overwrite file v2: standard_name = 'nox_emissions' (different)
    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
    cf_rc = nf90_def_dim(ncid, 'x', 5, dimid)
    cf_rc = nf90_def_var(ncid, 'myvar', NF90_FLOAT, [dimid], varid)
    cf_rc = nf90_put_att(ncid, varid, 'standard_name', 'nox_emissions')
    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    ! Re-read — must pick up new metadata
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache_v2, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL: v2 read failed'; rc = 1
    end if

    ! Property: new cache must reflect updated standard_name
    block
      integer :: iv
      logical :: found_nox
      found_nox = .false.
      do iv = 1, cache_v2%nvars
        if (trim(cache_v2%vars(iv)%standard_name) == 'nox_emissions') then
          found_nox = .true.; exit
        end if
      end do
      if (.not. found_nox) then
        write(*,*) 'FAIL: re-read cache does not contain updated standard_name'; rc = 1
      end if
    end block

    call cf_clear_cache(cache_v2)
    call PIO_Finalize(pio_sys, cf_rc)
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_invalidation_test

end program test_cf_property_17_cache_invalidation
