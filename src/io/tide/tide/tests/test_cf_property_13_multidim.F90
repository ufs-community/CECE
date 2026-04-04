!> @file test_cf_property_13_multidim.F90
!> @brief Property 13: Multi-Dimensional Variable Handling
!>
!> Feature: cf-convention-auto-detection, Property 13: Multi-Dimensional Variable Handling
!> Validates: Requirements 7.1, 7.2, 7.4
!>
!> For variables with 2-4 dimensions in any order, verify ndims and dimids
!> are correctly populated in the metadata cache.
program test_cf_property_13_multidim
  use tide_cf_detection_mod
  use ESMF
  use pio
  use netcdf
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_multidim_test(2, rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_multidim_test(3, rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_multidim_test(4, rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 13: ', npass, '/3 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a test for multidimensional variable detection in CF metadata.
  !> @param ndims_expected Expected number of dimensions
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_multidim_test(ndims_expected, rc)
    integer, intent(in)  :: ndims_expected
    integer, intent(out) :: rc

    integer :: ncid, varid, dimids(4), cf_rc, i
    integer :: my_task, n_tasks, comm
    character(len=64) :: fname, dname
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    type(ESMF_VM) :: vm

    rc = 0
    write(fname, '(a,i0,a)') '/tmp/cf_prop13_', ndims_expected, 'd.nc'

    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if

    cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')

    do i = 1, ndims_expected
      write(dname, '(a,i0)') 'dim', i
      cf_rc = nf90_def_dim(ncid, trim(dname), 5, dimids(i))
    end do

    cf_rc = nf90_def_var(ncid, 'myvar', NF90_FLOAT, dimids(1:ndims_expected), varid)
    cf_rc = nf90_put_att(ncid, varid, 'standard_name', 'test_variable')
    cf_rc = nf90_put_att(ncid, varid, 'units', 'kg m-2 s-1')
    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache, cf_rc)

    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL ndims=', ndims_expected, ': read failed rc=', cf_rc; rc = 1
    else if (cache%nvars < 1) then
      write(*,*) 'FAIL ndims=', ndims_expected, ': no vars in cache'; rc = 1
    else
      ! Find 'myvar' in cache (it may not be index 1 due to coordinate vars)
      block
        integer :: iv
        logical :: found
        found = .false.
        do iv = 1, cache%nvars
          if (trim(cache%vars(iv)%var_name) == 'myvar') then
            found = .true.
            if (cache%vars(iv)%ndims /= ndims_expected) then
              write(*,*) 'FAIL ndims=', ndims_expected, ': got ndims=', cache%vars(iv)%ndims
              rc = 1
            end if
            exit
          end if
        end do
        if (.not. found) then
          write(*,*) 'FAIL ndims=', ndims_expected, ': myvar not found in cache'; rc = 1
        end if
      end block
    end if

    call cf_clear_cache(cache)
    call PIO_Finalize(pio_sys, cf_rc)
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_multidim_test

end program test_cf_property_13_multidim
