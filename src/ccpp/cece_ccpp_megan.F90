module cece_ccpp_megan
  use iso_c_binding
  use cece_ccpp_state, only: g_cece_data_ptr, g_init_count, g_initialized, &
    cece_ccpp_core_finalize, cece_ccpp_core_init, cece_ccpp_get_export_field, &
    cece_ccpp_scheme_finalize, cece_ccpp_scheme_init, cece_ccpp_scheme_run, &
    cece_ccpp_set_import_field, cece_ccpp_sync_export_to_host, cece_ccpp_sync_import
  implicit none
  private
  public :: cece_ccpp_megan_init, cece_ccpp_megan_run, cece_ccpp_megan_finalize, &
            cece_ccpp_megan_timestep_init, cece_ccpp_megan_timestep_finalize


contains

  subroutine cece_ccpp_megan_init(horizontal_loop_extent, vertical_layer_dimension, &
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
        errmsg = 'cece_ccpp_megan_init: core init failed'
        return
      end if
      g_cece_data_ptr = new_ptr
      g_initialized = .true.
    end if

    call cece_ccpp_scheme_init(g_cece_data_ptr, 'megan'//c_null_char, 5, &
      horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then
      errflg = 1
      errmsg = 'cece_ccpp_megan_init: scheme init failed'
      return
    end if
    g_init_count = g_init_count + 1
  end subroutine

  subroutine cece_ccpp_megan_run(horizontal_loop_extent, vertical_layer_dimension, &
                            air_temperature, surface_pressure, &
                            soil_temperature, leaf_area_index, &
                            downward_shortwave_radiation, &
                            biogenic_isoprene_emission_flux, &
                            errmsg, errflg)
    integer, intent(in) :: horizontal_loop_extent, vertical_layer_dimension
    real(kind=8), intent(in) :: air_temperature(:,:)
    real(kind=8), intent(in) :: surface_pressure(:,:)
    real(kind=8), intent(in) :: soil_temperature(:,:)
    real(kind=8), intent(in) :: leaf_area_index(:,:)
    real(kind=8), intent(in) :: downward_shortwave_radiation(:,:)
    real(kind=8), intent(out) :: biogenic_isoprene_emission_flux(:,:)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc

    errmsg = ''
    errflg = 0

    ! Marshal inputs
    call cece_ccpp_set_import_field(g_cece_data_ptr, 'temperature'//c_null_char, 11, &
      air_temperature, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_run: set temperature failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, 'ps'//c_null_char, 2, &
      surface_pressure, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_run: set ps failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, 'soil_temperature'//c_null_char, 16, &
      soil_temperature, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_run: set soil_temperature failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, 'lai'//c_null_char, 3, &
      leaf_area_index, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_run: set lai failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, 'solar_radiation'//c_null_char, 15, &
      downward_shortwave_radiation, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_run: set solar_radiation failed'; return; end if

    ! Execute scheme
    call cece_ccpp_scheme_run(g_cece_data_ptr, 'megan'//c_null_char, 5, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_run: scheme run failed'; return; end if

    ! Marshal outputs
    call cece_ccpp_get_export_field(g_cece_data_ptr, 'EmissMEGAN_ISOP'//c_null_char, 15, &
      biogenic_isoprene_emission_flux, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_run: get EmissMEGAN_ISOP failed'; return; end if
  end subroutine

  subroutine cece_ccpp_megan_timestep_init(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_import(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_timestep_init: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_megan_timestep_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_export_to_host(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_timestep_finalize: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_megan_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0

    if (.not. g_initialized) then
      errflg = 1
      errmsg = 'cece_ccpp_megan_finalize: not initialized'
      return
    end if

    call cece_ccpp_scheme_finalize(g_cece_data_ptr, 'megan'//c_null_char, 5, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'megan_finalize: scheme finalize failed'; return; end if

    g_init_count = g_init_count - 1
    if (g_init_count <= 0) then
      call cece_ccpp_core_finalize(g_cece_data_ptr, rc)
      g_cece_data_ptr = c_null_ptr
      g_initialized = .false.
      g_init_count = 0
      if (rc /= 0) then; errflg = 1; errmsg = 'megan_finalize: core finalize failed'; return; end if
    end if
  end subroutine

end module cece_ccpp_megan
