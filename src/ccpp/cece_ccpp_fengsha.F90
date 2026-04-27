module cece_ccpp_fengsha
  use iso_c_binding
  use cece_ccpp_state, only: g_cece_data_ptr, g_init_count, g_initialized, &
    cece_ccpp_core_finalize, cece_ccpp_core_init, cece_ccpp_get_export_field, &
    cece_ccpp_scheme_finalize, cece_ccpp_scheme_init, cece_ccpp_scheme_run, &
    cece_ccpp_set_import_field, cece_ccpp_sync_export_to_host, cece_ccpp_sync_import
  implicit none
  private
  public :: cece_ccpp_fengsha_init, cece_ccpp_fengsha_run, &
            cece_ccpp_fengsha_finalize, &
            cece_ccpp_fengsha_timestep_init, cece_ccpp_fengsha_timestep_finalize

contains

  subroutine cece_ccpp_fengsha_init(horizontal_loop_extent, &
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
        errmsg = 'cece_ccpp_fengsha_init: core init failed'
        return
      end if
      g_cece_data_ptr = new_ptr
      g_initialized = .true.
    end if

    call cece_ccpp_scheme_init(g_cece_data_ptr, 'fengsha'//c_null_char, 7, &
      horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then
      errflg = 1
      errmsg = 'cece_ccpp_fengsha_init: scheme init failed'
      return
    end if
    g_init_count = g_init_count + 1
  end subroutine

  subroutine cece_ccpp_fengsha_run(horizontal_loop_extent, &
                                   vertical_layer_dimension, &
                                   friction_velocity, threshold_velocity, &
                                   soil_moisture, clay_fraction, &
                                   sand_fraction, silt_fraction, &
                                   erodibility, drag_partition, &
                                   air_density, lake_fraction, &
                                   snow_fraction, land_mask, &
                                   soil_temperature, &
                                   fengsha_dust_emission_flux, &
                                   errmsg, errflg)
    integer, intent(in) :: horizontal_loop_extent, vertical_layer_dimension
    real(kind=8), intent(in) :: friction_velocity(:,:)
    real(kind=8), intent(in) :: threshold_velocity(:,:)
    real(kind=8), intent(in) :: soil_moisture(:,:)
    real(kind=8), intent(in) :: clay_fraction(:,:)
    real(kind=8), intent(in) :: sand_fraction(:,:)
    real(kind=8), intent(in) :: silt_fraction(:,:)
    real(kind=8), intent(in) :: erodibility(:,:)
    real(kind=8), intent(in) :: drag_partition(:,:)
    real(kind=8), intent(in) :: air_density(:,:)
    real(kind=8), intent(in) :: lake_fraction(:,:)
    real(kind=8), intent(in) :: snow_fraction(:,:)
    real(kind=8), intent(in) :: land_mask(:,:)
    real(kind=8), intent(in) :: soil_temperature(:,:)
    real(kind=8), intent(out) :: fengsha_dust_emission_flux(:,:)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc

    errmsg = ''
    errflg = 0

    ! Marshal inputs
    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'friction_velocity'//c_null_char, 17, &
      friction_velocity, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set friction_velocity failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'threshold_velocity'//c_null_char, 18, &
      threshold_velocity, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set threshold_velocity failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'soil_moisture'//c_null_char, 13, &
      soil_moisture, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set soil_moisture failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'clay_fraction'//c_null_char, 13, &
      clay_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set clay_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'sand_fraction'//c_null_char, 13, &
      sand_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set sand_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'silt_fraction'//c_null_char, 13, &
      silt_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set silt_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'erodibility'//c_null_char, 11, &
      erodibility, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set erodibility failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'drag_partition'//c_null_char, 14, &
      drag_partition, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set drag_partition failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'air_density'//c_null_char, 11, &
      air_density, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set air_density failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'lake_fraction'//c_null_char, 13, &
      lake_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set lake_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'snow_fraction'//c_null_char, 13, &
      snow_fraction, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set snow_fraction failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'land_mask'//c_null_char, 9, &
      land_mask, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set land_mask failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, &
      'soil_temperature'//c_null_char, 16, &
      soil_temperature, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: set soil_temperature failed'; return; end if

    ! Execute scheme
    call cece_ccpp_scheme_run(g_cece_data_ptr, 'fengsha'//c_null_char, 7, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: scheme run failed'; return; end if

    ! Marshal outputs
    call cece_ccpp_get_export_field(g_cece_data_ptr, &
      'fengsha_dust_emissions'//c_null_char, 22, &
      fengsha_dust_emission_flux, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_run: get emissions failed'; return; end if
  end subroutine

  subroutine cece_ccpp_fengsha_timestep_init(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_import(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_timestep_init: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_fengsha_timestep_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_export_to_host(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_timestep_finalize: sync failed'; return; end if
  end subroutine

  subroutine cece_ccpp_fengsha_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0

    if (.not. g_initialized) then
      errflg = 1
      errmsg = 'cece_ccpp_fengsha_finalize: not initialized'
      return
    end if

    call cece_ccpp_scheme_finalize(g_cece_data_ptr, 'fengsha'//c_null_char, 7, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_finalize: scheme finalize failed'; return; end if

    g_init_count = g_init_count - 1
    if (g_init_count <= 0) then
      call cece_ccpp_core_finalize(g_cece_data_ptr, rc)
      g_cece_data_ptr = c_null_ptr
      g_initialized = .false.
      g_init_count = 0
      if (rc /= 0) then; errflg = 1; errmsg = 'fengsha_finalize: core finalize failed'; return; end if
    end if
  end subroutine

end module cece_ccpp_fengsha
