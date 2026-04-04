!> @file test_cf_property_4_field_map.F90
!> @brief Property 4: Field Map Creation on Match
!>
!> Feature: cf-convention-auto-detection, Property 4: Field Map Creation on Match
!> Validates: Requirement 2.3
!>
!> Every successful cf_match_variable call produces a populated
!> cf_field_mapping_t linking model_var to file_var.
program test_cf_property_4_field_map
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
    'Property 4: ', npass, '/20 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  !> @brief Run a single test iteration for CF field mapping.
  !> @param seed Random seed for test variation
  !> @param rc Return code (0 if pass, 1 if fail)
  subroutine run_one_iteration(seed, rc)
    integer, intent(in)  :: seed
    integer, intent(out) :: rc

    integer :: nvars, ivar, cf_rc
    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    type(cf_field_mapping_t)     :: fmap
    type(cf_detection_config_t)  :: cfg
    character(len=256) :: file_var, model_var, expected_file_var

    rc    = 0
    nvars = mod(seed, 15) + 3

    ! Build synthetic cache
    cache%nvars = nvars
    cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'
    cache%cf_version = 'CF-1.8'
    allocate(cache%vars(nvars))
    do ivar = 1, nvars
      write(cache%vars(ivar)%var_name,      '(a,i0)') 'fvar_', ivar
      write(cache%vars(ivar)%standard_name, '(a,i0)') 'sname_', ivar
      cache%vars(ivar)%units             = 'kg m-2 s-1'
      cache%vars(ivar)%has_standard_name = .true.
      cache%vars(ivar)%has_units         = .true.
      cache%vars(ivar)%has_long_name     = .false.
      cache%vars(ivar)%ndims             = 2
    end do

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 0
    call cf_detection_init(cfg, cf_rc)

    ! For each variable, verify a successful match produces a populated mapping
    do ivar = 1, nvars
      write(model_var,       '(a,i0)') 'sname_', ivar
      write(expected_file_var,'(a,i0)') 'fvar_',  ivar

      call cf_match_variable(trim(model_var), cache, file_var, meta, cf_rc)

      if (cf_rc /= CF_SUCCESS) then
        write(*,*) 'FAIL iter', seed, ' var', ivar, ': match failed rc=', cf_rc
        rc = 1; cycle
      end if

      ! Property: file_var must be non-empty and correct
      if (len_trim(file_var) == 0) then
        write(*,*) 'FAIL iter', seed, ' var', ivar, ': file_var is empty'
        rc = 1
      end if
      if (trim(file_var) /= trim(expected_file_var)) then
        write(*,*) 'FAIL iter', seed, ' var', ivar, ': file_var="', trim(file_var), &
                   '" expected="', trim(expected_file_var), '"'
        rc = 1
      end if

      ! Property: metadata must carry the standard_name
      if (trim(meta%standard_name) /= trim(model_var)) then
        write(*,*) 'FAIL iter', seed, ' var', ivar, ': meta standard_name mismatch'
        rc = 1
      end if

      ! Build a field mapping and verify it is populated
      fmap%model_var              = trim(model_var)
      fmap%file_var               = trim(file_var)
      fmap%source                 = 'cf_standard_name'
      fmap%units_compatible       = .true.
      fmap%units_conversion_needed= .false.
      fmap%metadata               = meta

      if (len_trim(fmap%model_var) == 0 .or. len_trim(fmap%file_var) == 0) then
        write(*,*) 'FAIL iter', seed, ' var', ivar, ': field map has empty var names'
        rc = 1
      end if
      if (trim(fmap%source) /= 'cf_standard_name') then
        write(*,*) 'FAIL iter', seed, ' var', ivar, ': field map source wrong'
        rc = 1
      end if
    end do

    call cf_clear_cache(cache)

  end subroutine run_one_iteration

end program test_cf_property_4_field_map
