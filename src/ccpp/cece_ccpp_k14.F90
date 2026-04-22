module cece_ccpp_k14
  use iso_c_binding
  use cece_ccpp_state, only: g_cece_data_ptr, g_init_count, g_initialized, &
    cece_ccpp_core_finalize, cece_ccpp_core_init, cece_ccpp_get_export_field, &
    cece_ccpp_scheme_finalize, cece_ccpp_scheme_init, cece_ccpp_scheme_run, &
    cece_ccpp_set_import_field, cece_ccpp_sync_export_to_host, &
    cece_ccpp_sync_import_to_device
  implicit none
  private
  public :: cece_ccpp_k14_init, cece_ccpp_k14_run, &
            cece_ccpp_k14_finalize, &
            cece_ccpp_k14_timestep_init, cece_ccpp_k14_timestep_finalize

contains

  subroutine cece_ccpp_k14_init(horizontal_loop_extent, &
                                vertical_layer_dimension, &
                                cece_configuration_file_path, &
                                errmsg, errflg)
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
        errmsg = 'cece_ccpp_k14_init: core init failed'
        return
      end if
      g_cece_data_ptr = new_ptr
      g_initialized = .true.
    end if

    call cece_ccpp_scheme_init(g_cece_data_ptr, 'k14'//c_null_char, 3, &
      horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then
      errflg = 1
      errmsg = 'cece_ccpp_k14_init: scheme init failed'
      return
    end if
    g_init_count = g_init_count + 1
  end subroutine

  subroutine cece_ccpp_k14_run(horizontal_loop_extent, &
                               vertical_layer_dimension, &
                               friction_velocity, soil_temperature, &
                               volumetric_soil_moisture, air_density, &
                               roughness_length, height, &
                               u_wind, v_wind, &
                               land_fraction, snow_fraction, &
                               dust_source, sand_fraction, &
                               silt_fraction, clay_fraction, &
                               soil_texture, vegetation_type, &
                               vegetation_fraction, &
                               k14_dust_emission_flux, &
                               errmsg, errflg)
    integer, intent(in) :: horizontal_loop_extent, vertical_layer_dimension
    real(kind=8), intent(in) :: friction_velocity(:,:)
    real(kind=8), intent(in) :: soil_temperature(:,:)
    real(kind=8), intent(in) :: volumetric_soil_moisture(:,:)
    real(kind=8), intent(in) :: air_density(:,:)
    real(kind=8), intent(in) :: roughness_length(:,:)
    real(kind=8), intent(in) :: height(:,:)
    real(kind=8), intent(in) :: u_wind(:,:)
    real(kind=8), intent(in) :: v_wind(:,:)
    real(kind=8), intent(in) :: land_fraction(:,:)
    real(kind=8), intent(in) :: snow_fraction(:,:)
    real(kind=8), intent(in) :: dust_source(:,:)
    real(kind=8), intent(in) :: sand_fraction(:,:)
    real(kind=8), intent(in) :: silt_fraction(:,:)
    real(kind=8), intent(in) :: clay_fraction(:,:)
    real(kind=8), intent(in) :: soil_texture(:,:)
    real(kind=8), intent(in) :: vegetation_type(:,:)
    real(kind=8), intent(in) :: vegetation_fraction(:,:)
    real(kind=8), intent(out) :: k14_dust_emission_flux(:,:)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc

    errmsg = ''
    errflg = 0

    ! Marshal inputs
    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'friction_velocity'//c_null_char, 17, &
      friction_velocity, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set friction_velocity failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'soil_temperature'//c_null_char, 16, &
      soil_temperature, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set soil_temperature failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'volumetric_soil_moisture'//c_null_char, 25, &
      volumetric_soil_moisture, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set volumetric_soil_moisture failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'air_density'//c_null_char, 11, &
      air_density, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set air_density failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'roughness_length'//c_null_char, 16, &
      roughness_length, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set roughness_length failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'height'//c_null_char, 6, &
      height, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set height failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'u_wind'//c_null_char, 6, &
      u_wind, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set u_wind failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'v_wind'//c_null_char, 6, &
      v_wind, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set v_wind failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'land_fraction'//c_null_char, 13, &
      land_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set land_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'snow_fraction'//c_null_char, 13, &
      snow_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set snow_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'dust_source'//c_null_char, 11, &
      dust_source, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set dust_source failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'sand_fraction'//c_null_char, 13, &
      sand_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set sand_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'silt_fraction'//c_null_char, 13, &
      silt_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set silt_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'clay_fraction'//c_null_char, 13, &
      clay_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set clay_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'soil_texture'//c_null_char, 12, &
      soil_texture, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set soil_texture failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'vegetation_type'//c_null_char, 15, &
      vegetation_type, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set vegetation_type failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'vegetation_fraction'//c_null_char, 19, &
      vegetation_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: set vegetation_fraction failed'; return; end if

    ! Execute scheme
    call cece_ccpp_scheme_run(g_cece_data_ptr, 'k14'//c_null_char, 3, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: scheme run failed'; return; end if

    ! Marshal outputs
    call cece_ccpp_get_export_field(g_cece_data_ptr, &
      'k14_dust_emissions'//c_null_char, 18, &
      k14_dust_emission_flux, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_run: get emissions failed'; return; end if
  end subroutine

  subroutine cece_ccpp_k14_timestep_init(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_import_to_device(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_timestep_init: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_k14_timestep_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_export_to_host(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_timestep_finalize: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_k14_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0

    if (.not. g_initialized) then
      errflg = 1
      errmsg = 'cece_ccpp_k14_finalize: not initialized'
      return
    end if

    call cece_ccpp_scheme_finalize(g_cece_data_ptr, 'k14'//c_null_char, 3, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'k14_finalize: scheme finalize failed'; return; end if

    g_init_count = g_init_count - 1
    if (g_init_count <= 0) then
      call cece_ccpp_core_finalize(g_cece_data_ptr, rc)
      g_cece_data_ptr = c_null_ptr
      g_initialized = .false.
      g_init_count = 0
      if (rc /= 0) then; errflg = 1; errmsg = 'k14_finalize: core finalize failed'; return; end if
    end if
  end subroutine

end module cece_ccpp_k14
