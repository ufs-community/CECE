module cece_ccpp_state
  use iso_c_binding
  implicit none
  private
  public :: g_cece_data_ptr, g_init_count, g_initialized

  type(c_ptr), save :: g_cece_data_ptr = c_null_ptr
  integer, save     :: g_init_count = 0
  logical, save     :: g_initialized = .false.
end module cece_ccpp_state
