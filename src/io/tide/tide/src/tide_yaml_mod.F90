!> @file tide_yaml_mod.F90
!> @brief Fortran interface to the TIDE C++ YAML parser.
module tide_yaml_mod
  use, intrinsic :: iso_c_binding
  implicit none

  !> @brief Fortran representation of the C tide_stream_config_t struct.
  type, bind(c) :: tide_stream_config_t
    type(c_ptr) :: name
    type(c_ptr) :: mesh_file
    type(c_ptr) :: lev_dimname
    type(c_ptr) :: tax_mode
    type(c_ptr) :: time_interp
    type(c_ptr) :: map_algo
    type(c_ptr) :: read_mode
    real(c_double) :: dt_limit
    integer(c_int) :: year_first
    integer(c_int) :: year_last
    integer(c_int) :: year_align
    integer(c_int) :: offset
    type(c_ptr) :: input_files
    integer(c_int) :: num_files
    type(c_ptr) :: file_vars
    type(c_ptr) :: model_vars
    integer(c_int) :: num_fields
    type(c_ptr) :: cf_detection_mode    !< CF detection mode: "auto", "strict", "disabled"
    integer(c_int) :: cf_cache_enabled  !< Enable CF metadata caching (1=true, 0=false)
    integer(c_int) :: cf_log_level      !< CF logging verbosity (0-3)
  end type tide_stream_config_t

  !> @brief Fortran representation of the C tide_config_t struct.
  type, bind(c) :: tide_config_t
    type(c_ptr) :: streams !< Array of tide_stream_config_t
    integer(c_int) :: num_streams !< Number of streams in the array
  end type tide_config_t

  interface
    !> @brief Parses a YAML file via the C++ interface.
    !> @param filename Null-terminated C string representing the path to the YAML file.
    !> @return Pointer to the parsed tide_config_t struct.
    function tide_parse_yaml(filename) bind(c, name="tide_parse_yaml")
      use, intrinsic :: iso_c_binding
      type(c_ptr), value :: filename !< Null-terminated C string
      type(c_ptr) :: tide_parse_yaml !< Pointer to tide_config_t
    end function tide_parse_yaml

    !> @brief Frees the configuration via the C++ interface.
    !> @param cfg Pointer to the tide_config_t struct to be freed.
    subroutine tide_free_config(cfg) bind(c, name="tide_free_config")
      use, intrinsic :: iso_c_binding
      type(c_ptr), value :: cfg !< Pointer to tide_config_t
    end subroutine tide_free_config
  end interface

contains

  !> @brief Converts a null-terminated C string to a Fortran string.
  !> @param cptr C pointer to the character array.
  !> @param fstr Output Fortran string.
  subroutine c_to_f_string(cptr, fstr)
    type(c_ptr), intent(in) :: cptr
    character(len=*), intent(out) :: fstr
    character(kind=c_char), pointer :: p(:)
    integer :: i, n

    ! Check if the C pointer is valid
    if (.not. c_associated(cptr)) then
      fstr = ' '
      return
    end if

    n = len(fstr)
    call c_f_pointer(cptr, p, [n])
    fstr = ' '

    ! Copy characters until null terminator is found
    do i = 1, n
      if (p(i) == c_null_char) exit
      fstr(i:i) = p(i)
    end do
  end subroutine c_to_f_string

end module tide_yaml_mod
