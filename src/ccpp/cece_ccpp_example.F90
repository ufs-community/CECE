module cece_ccpp_example
  use iso_c_binding
  use cece_ccpp_state, only: g_cece_data_ptr, g_init_count, g_initialized, &
    cece_ccpp_core_finalize, cece_ccpp_core_init, cece_ccpp_get_export_field, &
    cece_ccpp_scheme_finalize, cece_ccpp_scheme_init, cece_ccpp_scheme_run, &
    cece_ccpp_set_import_field, cece_ccpp_sync_export_to_host, cece_ccpp_sync_import
  implicit none
  private
  public :: cece_ccpp_example_init, cece_ccpp_example_run, cece_ccpp_example_finalize, &
            cece_ccpp_example_timestep_init, cece_ccpp_example_timestep_finalize


contains

  subroutine cece_ccpp_example_init(horizontal_loop_extent, vertical_layer_dimension, &
                               cece_configuration_file_path, errmsg, errflg)
    integer, intent(in) :: horizontal_loop_extent
    integer, intent(in) :: vertical_layer_dimension
    character(len=*), intent(in) :: cece_configuration_file_path
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    type(c_ptr) :: new_ptr

    errmsg = ''
    errflg = 0

    if (.not. g_initialized) then
      call cece_ccpp_core_init(new_ptr, &
        trim(cece_configuration_file_path)//c_null_char, &
        len_trim(cece_configuration_file_path), &
        horizontal_loop_extent, 1, vertical_layer_dimension, rc)
      if (rc /= 0) then
        errflg = 1
        errmsg = 'cece_ccpp_example_init: core init failed'
        return
      end if
      g_cece_data_ptr = new_ptr
      g_initialized = .true.
    end if

    call cece_ccpp_scheme_init(g_cece_data_ptr, 'example_emission'//c_null_char, 16, &
      horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then
      errflg = 1
      errmsg = 'cece_ccpp_example_init: scheme init failed'
      return
    end if
    g_init_count = g_init_count + 1
  end subroutine

  subroutine cece_ccpp_example_run(horizontal_loop_extent, vertical_layer_dimension, &
                              air_temperature, &
                              example_emission_flux, &
                              errmsg, errflg)
    integer, intent(in) :: horizontal_loop_extent, vertical_layer_dimension
    real(kind=8), intent(in) :: air_temperature(:,:)
    real(kind=8), intent(out) :: example_emission_flux(:,:)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc

    errmsg = ''
    errflg = 0

    ! Marshal inputs
    call cece_ccpp_set_import_field(g_cece_data_ptr, 'temperature'//c_null_char, 11, &
      air_temperature, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'example_run: set temperature failed'; return; end if

    ! Execute scheme
    call cece_ccpp_scheme_run(g_cece_data_ptr, 'example_emission'//c_null_char, 16, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'example_run: scheme run failed'; return; end if

    ! Marshal outputs
    call cece_ccpp_get_export_field(g_cece_data_ptr, 'EmissExample'//c_null_char, 12, &
      example_emission_flux, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'example_run: get EmissExample failed'; return; end if
  end subroutine

  subroutine cece_ccpp_example_timestep_init(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_import(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'example_timestep_init: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_example_timestep_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_export_to_host(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'example_timestep_finalize: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_example_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0

    if (.not. g_initialized) then
      errflg = 1
      errmsg = 'cece_ccpp_example_finalize: not initialized'
      return
    end if

    call cece_ccpp_scheme_finalize(g_cece_data_ptr, 'example_emission'//c_null_char, 16, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'example_finalize: scheme finalize failed'; return; end if

    g_init_count = g_init_count - 1
    if (g_init_count <= 0) then
      call cece_ccpp_core_finalize(g_cece_data_ptr, rc)
      g_cece_data_ptr = c_null_ptr
      g_initialized = .false.
      g_init_count = 0
      if (rc /= 0) then; errflg = 1; errmsg = 'example_finalize: core finalize failed'; return; end if
    end if
  end subroutine

end module cece_ccpp_example
