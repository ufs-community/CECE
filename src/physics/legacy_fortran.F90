!> @file legacy_fortran.F90
!> @brief Legacy Fortran physics implementation for testing the bridge.
!>
!> This module contains a simple physics kernel to demonstrate
!> interoperability with C++ ACES components.

module legacy_fortran_mod
  use iso_c_binding
  implicit none

contains

  !> @brief Run legacy Fortran physics calculation.
  !>
  !> Computes NOx emissions based on temperature and wind speed.
  !> exposed to C++ via bind(C)
  subroutine run_legacy_fortran(temp, wind, nox, nx, ny, nz) bind(C, name="run_legacy_fortran")
    real(c_double), intent(in)  :: temp(nx, ny, nz) !< Air temperature (K)
    real(c_double), intent(in)  :: wind(nx, ny, nz) !< Wind speed (m/s)
    real(c_double), intent(out) :: nox(nx, ny, nz)  !< Calculated NOx
    integer(c_int), value, intent(in) :: nx, ny, nz

    integer :: i, j, k

    ! Simple Kernel: NOx = temp * 0.1 + wind * 0.5
    ! This is purely for demonstration purposes.
    do k = 1, nz
       do j = 1, ny
          do i = 1, nx
             nox(i,j,k) = temp(i,j,k) * 0.1d0 + wind(i,j,k) * 0.5d0
          end do
       end do
    end do

  end subroutine run_legacy_fortran

end module legacy_fortran_mod
