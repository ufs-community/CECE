!> @file test_cf_property_21_compliance.F90
!> @brief Property 21: CF Compliance Detection
!>
!> Feature: cf-convention-auto-detection, Property 21: CF Compliance Detection
!> Validates: Requirements 10.1, 10.2, 10.4
!>
!> For any NetCDF file, the Detection_Engine SHALL check for the Conventions
!> global attribute and enable CF detection if it indicates CF-1.6/1.7/1.8/1.9.
program test_cf_property_21_compliance
  use tide_cf_detection_mod
  use ESMF
  use pio
  use netcdf
  implicit none

  integer :: rc, overall_rc, npass, nfail
  character(len=8) :: versions(4)
  integer :: iv

  overall_rc = 0; npass = 0; nfail = 0
  versions = ['CF-1.6  ', 'CF-1.7  ', 'CF-1.8  ', 'CF-1.9  ']

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  ! Test each supported CF version
  do iv = 1, 4
    call run_version_test(trim(versions(iv)), .true., rc)
    if (rc == 0) then; npass = npass + 1
    else; nfail = nfail + 1; overall_rc = 1
    end if
  end do

  ! Test non-CF conventions → should NOT be compliant
  call run_version_test('COARDS', .false., rc)
  if (rc == 0) then; npass = npass + 1
  else; nfail = nfail + 1; overall_rc = 1
  end if

  ! Test absent Conventions → should NOT be compliant
  call run_no_conventions_test(rc)
  if (rc == 0) then; npass = npass + 1
  else; nfail = nfail + 1; overall_rc = 1
  end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 21: ', npass, '/6 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a test for CF compliance with a given conventions string.
  !> @param conv_str Conventions string to test
  !> @param expect_compliant Expected compliance (True/False)
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_version_test(conv_str, expect_compliant, rc)
    character(len=*), intent(in)  :: conv_str
    logical,          intent(in)  :: expect_compliant
    integer,          intent(out) :: rc

    integer :: ncid, dimid, cf_rc, my_task, n_tasks, comm
    character(len=64) :: fname
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    type(ESMF_VM) :: vm

    rc = 0
    write(fname, '(a,a,a)') '/tmp/cf_prop21_', trim(conv_str), '.nc'
    ! Replace slashes in filename
    call replace_char(fname, '/', '_')

    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if
    cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', trim(conv_str))
    cf_rc = nf90_def_dim(ncid, 'x', 2, dimid)
    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache, cf_rc)

    if (cache%is_cf_compliant .neqv. expect_compliant) then
      write(*,*) 'FAIL Conventions="'//trim(conv_str)//'": is_cf_compliant=', &
                 cache%is_cf_compliant, ' expected', expect_compliant
      rc = 1
    end if

    call cf_clear_cache(cache)
    call PIO_Finalize(pio_sys, cf_rc)
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_version_test

  !> @brief Run a test for CF compliance with no conventions attribute.
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_no_conventions_test(rc)
    integer, intent(out) :: rc
    integer :: ncid, dimid, cf_rc, my_task, n_tasks, comm
    character(len=64) :: fname
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    type(ESMF_VM) :: vm

    rc = 0
    fname = '/tmp/cf_prop21_no_conventions.nc'
    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if
    cf_rc = nf90_def_dim(ncid, 'x', 2, dimid)
    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache, cf_rc)

    if (cache%is_cf_compliant) then
      write(*,*) 'FAIL: file with no Conventions should not be CF compliant'
      rc = 1
    end if

    call cf_clear_cache(cache)
    call PIO_Finalize(pio_sys, cf_rc)
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_no_conventions_test

  subroutine replace_char(str, old_c, new_c)
    character(len=*), intent(inout) :: str
    character(len=1), intent(in)    :: old_c, new_c
    integer :: i
    do i = 1, len_trim(str)
      if (str(i:i) == old_c) str(i:i) = new_c
    end do
  end subroutine replace_char

end program test_cf_property_21_compliance
