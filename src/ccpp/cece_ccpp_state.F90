module cece_ccpp_state
  use iso_c_binding
  implicit none
  private
  public :: g_cece_data_ptr, g_init_count, g_initialized
  public :: cece_ccpp_core_init, cece_ccpp_scheme_init
  public :: cece_ccpp_set_import_field, cece_ccpp_scheme_run
  public :: cece_ccpp_get_export_field, cece_ccpp_scheme_finalize
  public :: cece_ccpp_core_finalize
  public :: cece_ccpp_sync_import_to_device, cece_ccpp_sync_export_to_host
  public :: cece_ccpp_run_stacking

  type(c_ptr), save :: g_cece_data_ptr = c_null_ptr
  integer, save     :: g_init_count = 0
  logical, save     :: g_initialized = .false.

  ! C API interfaces — shared by all CCPP driver schemes
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

    subroutine cece_ccpp_run_stacking(data_ptr, hour, day_of_week, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: hour, day_of_week
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

end module cece_ccpp_state
