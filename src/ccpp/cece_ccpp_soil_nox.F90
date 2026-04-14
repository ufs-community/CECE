module cece_ccpp_soil_nox
  use iso_c_binding
  use cece_ccpp_state, only: g_cece_data_ptr, g_init_count, g_initialized
  implicit none
  private
  public :: cece_soil_nox_init, cece_soil_nox_run, cece_soil_nox_finalize, &
            cece_soil_nox_timestep_init, cece_soil_nox_timestep_finalize

  ! C API interfaces
  interface
    subroutine cece_ccpp_core_init(data_ptr, config_path, path_len, &
                                   nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), intent(out) :: data_ptr
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len, nx, ny, nz
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_scheme_init(data_ptr, name, name_len, &
                                     nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len, nx, ny, nz
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_set_import_field(data_ptr, name, name_len, &
                                          field_data, nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_char, c_int, c_double
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len, nx, ny, nz
      real(c_double), intent(in) :: field_data(nx, ny, nz)
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_scheme_run(data_ptr, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_get_export_field(data_ptr, name, name_len, &
                                          field_data, nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_char, c_int, c_double
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len, nx, ny, nz
      real(c_double), intent(out) :: field_data(nx, ny, nz)
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_scheme_finalize(data_ptr, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_core_finalize(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_sync_import_to_device(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_ccpp_sync_export_to_host(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

contains

  subroutine cece_soil_nox_init(horizontal_loop_extent, vertical_layer_dimension, &
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
        errmsg = 'cece_soil_nox_init: core init failed'
        return
      end if
      g_cece_data_ptr = new_ptr
      g_initialized = .true.
    end if

    call cece_ccpp_scheme_init(g_cece_data_ptr, 'soil_nox'//c_null_char, 8, &
      horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then
      errflg = 1
      errmsg = 'cece_soil_nox_init: scheme init failed'
      return
    end if
    g_init_count = g_init_count + 1
  end subroutine

  subroutine cece_soil_nox_run(horizontal_loop_extent, vertical_layer_dimension, &
                               soil_temperature, soil_moisture, &
                               soil_nox_emission_flux, &
                               errmsg, errflg)
    integer, intent(in) :: horizontal_loop_extent, vertical_layer_dimension
    real(kind=8), intent(in) :: soil_temperature(:,:)
    real(kind=8), intent(in) :: soil_moisture(:,:)
    real(kind=8), intent(out) :: soil_nox_emission_flux(:,:)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc

    errmsg = ''
    errflg = 0

    ! Marshal inputs
    call cece_ccpp_set_import_field(g_cece_data_ptr, 'soil_temperature'//c_null_char, 16, &
      soil_temperature, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_run: set soil_temperature failed'; return; end if

    call cece_ccpp_set_import_field(g_cece_data_ptr, 'soil_moisture'//c_null_char, 13, &
      soil_moisture, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_run: set soil_moisture failed'; return; end if

    ! Execute scheme
    call cece_ccpp_scheme_run(g_cece_data_ptr, 'soil_nox'//c_null_char, 8, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_run: scheme run failed'; return; end if

    ! Marshal outputs
    call cece_ccpp_get_export_field(g_cece_data_ptr, 'EmissSoilNOx'//c_null_char, 12, &
      soil_nox_emission_flux, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_run: get EmissSoilNOx failed'; return; end if
  end subroutine

  subroutine cece_soil_nox_timestep_init(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_import_to_device(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_timestep_init: sync failed'; return; end if
  end subroutine

  subroutine cece_soil_nox_timestep_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_export_to_host(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_timestep_finalize: sync failed'; return; end if
  end subroutine

  subroutine cece_soil_nox_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0

    if (.not. g_initialized) then
      errflg = 1
      errmsg = 'cece_soil_nox_finalize: not initialized'
      return
    end if

    call cece_ccpp_scheme_finalize(g_cece_data_ptr, 'soil_nox'//c_null_char, 8, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_finalize: scheme finalize failed'; return; end if

    g_init_count = g_init_count - 1
    if (g_init_count <= 0) then
      call cece_ccpp_core_finalize(g_cece_data_ptr, rc)
      g_cece_data_ptr = c_null_ptr
      g_initialized = .false.
      g_init_count = 0
      if (rc /= 0) then; errflg = 1; errmsg = 'soil_nox_finalize: core finalize failed'; return; end if
    end if
  end subroutine

end module cece_ccpp_soil_nox
