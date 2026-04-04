!> @file test_cf_property_22_noncf_fallback.F90
!> @brief Property 22: Non-CF Fallback
!>
!> Feature: cf-convention-auto-detection, Property 22: Non-CF Fallback
!> Validates: Requirement 10.3
!>
!> For any NetCDF file where the Conventions attribute is absent or indicates
!> a non-CF format, the Detection_Engine SHALL log a warning and use Fallback_Mode
!> (is_cf_compliant = .false.).
program test_cf_property_22_noncf_fallback
  use tide_cf_detection_mod
  use ESMF
  use pio
  use netcdf
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  ! Case 1: absent Conventions
  call run_test('', .false., rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  ! Case 2: COARDS (non-CF)
  call run_test('COARDS', .false., rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  ! Case 3: arbitrary string
  call run_test('MyConvention-2.0', .false., rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  ! Case 4: CF-1.8 (should be compliant — sanity check)
  call run_test('CF-1.8', .true., rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 22: ', npass, '/4 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a test for CF compliance fallback with given conventions string.
  !> @param conv_str Conventions string to test
  !> @param expect_compliant Expected compliance (True/False)
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_test(conv_str, expect_compliant, rc)
    character(len=*), intent(in)  :: conv_str
    logical,          intent(in)  :: expect_compliant
    integer,          intent(out) :: rc

    integer :: ncid, dimid, cf_rc, my_task, n_tasks, comm
    character(len=80) :: fname, safe_conv
    type(cf_metadata_cache_t) :: cache
    type(cf_detection_config_t) :: cfg
    type(iosystem_desc_t), target :: pio_sys
    type(ESMF_VM) :: vm

    rc = 0
    safe_conv = trim(conv_str)
    call sanitize_filename(safe_conv)
    if (len_trim(safe_conv) == 0) safe_conv = 'absent'
    write(fname, '(a,a,a)') '/tmp/cf_prop22_', trim(safe_conv), '.nc'

    cf_rc = nf90_create(trim(fname), NF90_CLOBBER, ncid)
    if (cf_rc /= NF90_NOERR) then; rc = 1; return; end if
    if (len_trim(conv_str) > 0) then
      cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', trim(conv_str))
    end if
    cf_rc = nf90_def_dim(ncid, 'x', 2, dimid)
    cf_rc = nf90_enddef(ncid)
    cf_rc = nf90_close(ncid)

    call ESMF_VMGetCurrent(vm, rc=cf_rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=cf_rc)
    call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, pio_sys)

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)
    call cf_read_file_metadata(trim(fname), pio_sys, PIO_IOTYPE_NETCDF, cache, cf_rc)

    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL conv="'//trim(conv_str)//'": read returned rc=', cf_rc
      rc = 1
    end if

    if (cache%is_cf_compliant .neqv. expect_compliant) then
      write(*,*) 'FAIL conv="'//trim(conv_str)//'": is_cf_compliant=', &
                 cache%is_cf_compliant, ' expected', expect_compliant
      rc = 1
    end if

    call cf_clear_cache(cache)
    call PIO_Finalize(pio_sys, cf_rc)
    open(unit=99, file=trim(fname), status='old', iostat=cf_rc)
    if (cf_rc == 0) close(99, status='delete')

  end subroutine run_test

  !> @brief Sanitize a filename by replacing unsafe characters with '_'.
  !> @param s Filename string (inout)
  subroutine sanitize_filename(s)
    character(len=*), intent(inout) :: s
    integer :: i
    do i = 1, len_trim(s)
      select case (s(i:i))
        case ('/', '\', ':', '*', '?', '"', '<', '>', '|', ' ')
          s(i:i) = '_'
      end select
    end do
  end subroutine sanitize_filename

end program test_cf_property_22_noncf_fallback
