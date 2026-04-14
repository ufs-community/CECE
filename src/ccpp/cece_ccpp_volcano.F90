module cece_ccpp_volcano
  use iso_c_binding
  use cece_ccpp_state, only: g_cece_data_ptr, g_init_count, g_initialized
  implicit none
  private
  public :: cece_volcano_init, cece_volcano_run, cece_volcano_finalize, &
            cece_volcano_timestep_init, cece_volcano_timestep_finalize

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

  subroutine cece_volcano_init(horizontal_loop_extent, vertical_layer_dimension, &
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
        errmsg = 'cece_volcano_init: core init failed'
        return
      end if
      g_cece_data_ptr = new_ptr
      g_initialized = .true.
    end if

    call cece_ccpp_scheme_init(g_cece_data_ptr, 'volcano'//c_null_char, 7, &
      horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then
      errflg = 1
      errmsg = 'cece_volcano_init: scheme init failed'
      return
    end if
    g_init_count = g_init_count + 1
  end subroutine

  subroutine cece_volcano_run(horizontal_loop_extent, vertical_layer_dimension, &
                              volcanic_so2_emission_flux, &
                              errmsg, errflg)
    integer, intent(in) :: horizontal_loop_extent, vertical_layer_dimension
    real(kind=8), intent(out) :: volcanic_so2_emission_flux(:,:)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc

    errmsg = ''
    errflg = 0

    ! No inputs — volcano uses config-based data

    ! Execute scheme
    call cece_ccpp_scheme_run(g_cece_data_ptr, 'volcano'//c_null_char, 7, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'volcano_run: scheme run failed'; return; end if

    ! Marshal outputs
    call cece_ccpp_get_export_field(g_cece_data_ptr, 'EmissVolcSO2'//c_null_char, 12, &
      volcanic_so2_emission_flux, horizontal_loop_extent, 1, vertical_layer_dimension, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'volcano_run: get EmissVolcSO2 failed'; return; end if
  end subroutine

  subroutine cece_volcano_timestep_init(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_import_to_device(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'volcano_timestep_init: sync failed'; return; end if
  end subroutine

  subroutine cece_volcano_timestep_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0
    call cece_ccpp_sync_export_to_host(g_cece_data_ptr, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'volcano_timestep_finalize: sync failed'; return; end if
  end subroutine

  subroutine cece_volcano_finalize(errmsg, errflg)
    character(len=512), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int) :: rc
    errmsg = ''
    errflg = 0

    if (.not. g_initialized) then
      errflg = 1
      errmsg = 'cece_volcano_finalize: not initialized'
      return
    end if

    call cece_ccpp_scheme_finalize(g_cece_data_ptr, 'volcano'//c_null_char, 7, rc)
    if (rc /= 0) then; errflg = 1; errmsg = 'volcano_finalize: scheme finalize failed'; return; end if

    g_init_count = g_init_count - 1
    if (g_init_count <= 0) then
      call cece_ccpp_core_finalize(g_cece_data_ptr, rc)
      g_cece_data_ptr = c_null_ptr
      g_initialized = .false.
      g_init_count = 0
      if (rc /= 0) then; errflg = 1; errmsg = 'volcano_finalize: core finalize failed'; return; end if
    end if
  end subroutine

end module cece_ccpp_volcano
