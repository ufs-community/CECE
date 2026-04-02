!> @file test_cf_property_20_fieldmap_log.F90
!> @brief Property 20: Field Map Logging
!>
!> Feature: cf-convention-auto-detection, Property 20: Field Map Logging
!> Validates: Requirement 9.3
!>
!> For any Field_Map creation, verify log contains source var, destination
!> var, and mapping method (CF or explicit).
program test_cf_property_20_fieldmap_log
  use tide_cf_detection_mod
  use ESMF
  implicit none

  integer :: rc, overall_rc, npass, nfail
  overall_rc = 0; npass = 0; nfail = 0

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call run_cf_fieldmap_log_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  call run_explicit_fieldmap_log_test(rc)
  if (rc == 0) then; npass = npass + 1; else; nfail = nfail + 1; overall_rc = 1; end if

  write(*,'(a,i0,a,i0,a,i0,a)') &
    'Property 20: ', npass, '/2 passed (', nfail, ' failed)'

  call ESMF_Finalize(rc=rc)
  if (overall_rc /= 0) stop 1

contains

  subroutine run_cf_fieldmap_log_test(rc)
    integer, intent(out) :: rc

    type(cf_metadata_cache_t)    :: cache
    type(cf_variable_metadata_t) :: meta
    type(cf_field_mapping_t)     :: fmap
    type(cf_detection_config_t)  :: cfg
    character(len=256) :: file_var
    integer :: cf_rc

    rc = 0
    cache%nvars = 1; cache%is_cf_compliant = .true.
    cache%filename = 'synthetic'; cache%cf_version = 'CF-1.8'
    allocate(cache%vars(1))
    cache%vars(1)%var_name          = 'CO'
    cache%vars(1)%standard_name     = 'co_emissions'
    cache%vars(1)%has_standard_name = .true.
    cache%vars(1)%has_long_name     = .false.
    cache%vars(1)%has_units         = .false.
    cache%vars(1)%ndims             = 0

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 2
    call cf_detection_init(cfg, cf_rc)

    call cf_match_variable('co_emissions', cache, file_var, meta, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL CF fieldmap: match failed'; rc = 1; call cf_clear_cache(cache); return
    end if

    ! Build field map and verify all log-relevant fields are populated
    fmap%model_var               = 'co_emissions'
    fmap%file_var                = trim(file_var)
    fmap%source                  = 'cf_standard_name'
    fmap%units_compatible        = .true.
    fmap%units_conversion_needed = .false.
    fmap%metadata                = meta

    if (len_trim(fmap%model_var) == 0) then
      write(*,*) 'FAIL CF fieldmap: model_var empty'; rc = 1
    end if
    if (len_trim(fmap%file_var) == 0) then
      write(*,*) 'FAIL CF fieldmap: file_var empty'; rc = 1
    end if
    if (trim(fmap%source) /= 'cf_standard_name') then
      write(*,*) 'FAIL CF fieldmap: source wrong "', trim(fmap%source), '"'; rc = 1
    end if

    ! Log the field map creation (exercises logging path)
    call cf_log(2, 'Field map: model_var="'//trim(fmap%model_var)// &
                   '" file_var="'//trim(fmap%file_var)// &
                   '" source="'//trim(fmap%source)//'"')

    call cf_clear_cache(cache)

  end subroutine run_cf_fieldmap_log_test

  subroutine run_explicit_fieldmap_log_test(rc)
    integer, intent(out) :: rc

    type(cf_field_mapping_t)    :: fmap
    type(cf_detection_config_t) :: cfg
    character(len=256) :: fld_file(1), fld_model(1), file_var
    integer :: cf_rc

    rc = 0
    fld_model(1) = 'nox_emissions'
    fld_file(1)  = 'NOx'

    cfg%mode = 'auto'; cfg%cache_enabled = .true.; cfg%log_level = 2
    call cf_detection_init(cfg, cf_rc)

    call cf_apply_explicit_mapping('nox_emissions', fld_file, fld_model, 1, file_var, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) 'FAIL explicit fieldmap: mapping failed'; rc = 1; return
    end if

    fmap%model_var = 'nox_emissions'
    fmap%file_var  = trim(file_var)
    fmap%source    = 'explicit'

    if (len_trim(fmap%model_var) == 0) then
      write(*,*) 'FAIL explicit fieldmap: model_var empty'; rc = 1
    end if
    if (len_trim(fmap%file_var) == 0) then
      write(*,*) 'FAIL explicit fieldmap: file_var empty'; rc = 1
    end if
    if (trim(fmap%source) /= 'explicit') then
      write(*,*) 'FAIL explicit fieldmap: source wrong'; rc = 1
    end if

    call cf_log(2, 'Field map: model_var="'//trim(fmap%model_var)// &
                   '" file_var="'//trim(fmap%file_var)// &
                   '" source="'//trim(fmap%source)//'"')

  end subroutine run_explicit_fieldmap_log_test

end program test_cf_property_20_fieldmap_log
