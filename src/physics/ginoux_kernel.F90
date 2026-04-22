!> @file ginoux_kernel.F90
!> @brief Standalone Ginoux (GOCART2G) dust emission kernel with bind(C) interface.
!>
!> This module provides a C-callable entry point for the Ginoux dust
!> emission computation, following the same bridge pattern as fengsha_kernel.F90.
!> The algorithm is ported from the DustEmissionGOCART2G subroutine in
!> cece_fengsha.F90, preserving its numerical behavior exactly.
!>
!> @author CECE Development Team
!> @date 2025
!> @version 1.0
module ginoux_kernel_mod
    use iso_c_binding
    implicit none

    ! Constants
    integer, parameter :: LAND = 1
    real(c_double), parameter :: AIR_DENS = 1.25d0       ! assumed air density [kg/m^3]
    real(c_double), parameter :: SOIL_DENSITY = 2650.0d0  ! soil particle density [kg/m^3]

contains

    !> @brief Compute Ginoux (GOCART2G) dust emissions (C-callable entry point).
    !>
    !> Converts C pointers to Fortran arrays and executes the Ginoux
    !> dust emission algorithm over the 2D grid. The algorithm computes
    !> per-bin Marticorena dry-soil threshold velocity, applies Ginoux
    !> moisture modification, and uses a wind-speed-cubed emission formula.
    !>
    !> @param[in]  radius_ptr      Per-bin particle radii pointer [m]
    !> @param[in]  fraclake_ptr    Lake fraction pointer [1]
    !> @param[in]  gwettop_ptr     Surface soil wetness pointer [1]
    !> @param[in]  oro_ptr         Land-ocean-ice mask pointer [1]
    !> @param[in]  u10m_ptr        10-meter eastward wind pointer [m/s]
    !> @param[in]  v10m_ptr        10-meter northward wind pointer [m/s]
    !> @param[in]  du_src_ptr      Dust source strength pointer [(s^2 m^5)/kg]
    !> @param[in]  emissions_ptr   Output: binned emissions pointer [kg/(m^2 s)]
    !> @param[in]  nx              Grid x-dimension
    !> @param[in]  ny              Grid y-dimension
    !> @param[in]  nbins           Number of dust size bins
    !> @param[in]  Ch_DU           Dust emission tuning coefficient
    !> @param[in]  grav            Gravity [m/s^2]
    subroutine run_ginoux_fortran( &
        radius_ptr, fraclake_ptr, gwettop_ptr, oro_ptr, &
        u10m_ptr, v10m_ptr, du_src_ptr, emissions_ptr, &
        nx, ny, nbins, Ch_DU, grav &
    ) bind(c, name="run_ginoux_fortran")

        ! Pointer arguments
        type(c_ptr), value :: radius_ptr
        type(c_ptr), value :: fraclake_ptr, gwettop_ptr, oro_ptr
        type(c_ptr), value :: u10m_ptr, v10m_ptr
        type(c_ptr), value :: du_src_ptr
        type(c_ptr), value :: emissions_ptr

        ! Scalar arguments
        integer(c_int), value :: nx, ny, nbins
        real(c_double), value :: Ch_DU, grav

        ! Local Fortran array pointers
        real(c_double), pointer :: radius(:)
        real(c_double), pointer :: fraclake(:,:), gwettop(:,:), oro(:,:)
        real(c_double), pointer :: u10m(:,:), v10m(:,:)
        real(c_double), pointer :: du_src(:,:)
        real(c_double), pointer :: emissions(:,:,:)

        ! Local variables
        integer :: i, j, n
        real(c_double) :: diameter
        real(c_double) :: u_thresh0
        real(c_double) :: u_thresh
        real(c_double) :: w10m

        ! Convert C pointers to Fortran arrays
        call c_f_pointer(radius_ptr, radius, [nbins])
        call c_f_pointer(fraclake_ptr, fraclake, [nx, ny])
        call c_f_pointer(gwettop_ptr, gwettop, [nx, ny])
        call c_f_pointer(oro_ptr, oro, [nx, ny])
        call c_f_pointer(u10m_ptr, u10m, [nx, ny])
        call c_f_pointer(v10m_ptr, v10m, [nx, ny])
        call c_f_pointer(du_src_ptr, du_src, [nx, ny])
        call c_f_pointer(emissions_ptr, emissions, [nx, ny, nbins])

        ! Initialize emissions to zero
        emissions = 0.0d0

        ! Calculate per-bin emissions
        ! Following the original DustEmissionGOCART2G algorithm exactly
        do n = 1, nbins
            diameter = 2.0d0 * radius(n)

            ! Marticorena dry-soil threshold velocity [m/s]
            u_thresh0 = 0.13d0 &
                * sqrt(SOIL_DENSITY * grav * diameter / AIR_DENS) &
                * sqrt(1.0d0 + 6.0d-7 / (SOIL_DENSITY * grav * diameter**2.5d0)) &
                / sqrt(1.928d0 * (1331.0d0 * (100.0d0 * diameter)**1.56d0 + 0.38d0)**0.092d0 - 1.0d0)

            ! Spatially dependent part of calculation
            do j = 1, ny
                do i = 1, nx
                    ! Only over LAND gridpoints
                    if (nint(oro(i,j)) /= LAND) cycle

                    w10m = sqrt(u10m(i,j)**2.0d0 + v10m(i,j)**2.0d0)

                    ! Modify the threshold depending on soil moisture
                    ! as in Ginoux et al. [2001]
                    if (gwettop(i,j) .lt. 0.5d0) then
                        u_thresh = max(0.0d0, u_thresh0 &
                            * (1.2d0 + 0.2d0 * log10(max(1.0d-3, gwettop(i,j)))))

                        if (w10m .gt. u_thresh) then
                            ! Emission of dust [kg m-2 s-1]
                            emissions(i,j,n) = (1.0d0 - fraclake(i,j)) &
                                * w10m**2.0d0 * (w10m - u_thresh)
                        end if
                    end if
                end do
            end do

            ! Scale by tuning coefficient and source strength
            emissions(:,:,n) = Ch_DU * du_src(:,:) * emissions(:,:,n)
        end do

    end subroutine run_ginoux_fortran

end module ginoux_kernel_mod
