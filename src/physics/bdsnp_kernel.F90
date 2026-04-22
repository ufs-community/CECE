!> @file bdsnp_kernel.F90
!> @brief Fortran kernel for the BDSNP soil NO emission scheme.
!>
!> Implements the run_bdsnp_fortran subroutine with both YL95 and BDSNP
!> algorithms matching the C++ BdsnpScheme logic. Produces numerically
!> identical results to the C++ BdsnpScheme within 1e-6 relative tolerance.
!>
!> @author CECE Team
!> @date 2024

module bdsnp_kernel_mod
    use iso_c_binding
    implicit none

    ! Constants matching the C++ BdsnpScheme implementation
    real(c_double), parameter :: FREEZING_C = 0.0d0
    real(c_double), parameter :: MW_NO = 30.0d0
    real(c_double), parameter :: UNITCONV = 1.0d-12 / 14.0d0 * MW_NO

    ! YL95 default parameters
    real(c_double), parameter :: A_BIOME_WET = 0.5d0
    real(c_double), parameter :: TC_MAX = 30.0d0
    real(c_double), parameter :: EXP_COEFF = 0.103d0
    real(c_double), parameter :: WET_C1 = 5.5d0
    real(c_double), parameter :: WET_C2 = -5.55d0

    ! BDSNP default parameters
    real(c_double), parameter :: BDSNP_BASE_EF = 1.0d0
    real(c_double), parameter :: BDSNP_SM_THRESH1 = 0.3d0
    real(c_double), parameter :: BDSNP_SM_SLOPE2 = 0.5d0
    real(c_double), parameter :: BDSNP_SM_RANGE2 = 0.7d0

contains

    !> @brief YL95 soil temperature response function.
    pure function soil_temp_term_yl95(tc) result(t)
        real(c_double), intent(in) :: tc
        real(c_double) :: t

        if (tc <= FREEZING_C) then
            t = 0.0d0
            return
        end if
        t = exp(EXP_COEFF * min(TC_MAX, tc))
    end function soil_temp_term_yl95

    !> @brief YL95 soil moisture response function.
    pure function soil_wet_term_yl95(gw) result(w)
        real(c_double), intent(in) :: gw
        real(c_double) :: w

        w = WET_C1 * gw * exp(WET_C2 * gw * gw)
    end function soil_wet_term_yl95

    !> @brief BDSNP soil moisture dependence factor.
    !>
    !> Piecewise linear response matching C++ bdsnp_moisture_factor:
    !>   SM <= 0.0: 0.0
    !>   SM <= 0.3: SM / 0.3 (ramp up to 1.0)
    !>   SM >  0.3: 1.0 - 0.5 * (SM - 0.3) / 0.7 (decrease to 0.5 at SM=1.0)
    pure function bdsnp_moisture_factor(soil_moisture) result(f)
        real(c_double), intent(in) :: soil_moisture
        real(c_double) :: f

        if (soil_moisture <= 0.0d0) then
            f = 0.0d0
        else if (soil_moisture <= BDSNP_SM_THRESH1) then
            f = soil_moisture / BDSNP_SM_THRESH1
        else
            f = 1.0d0 - BDSNP_SM_SLOPE2 * (soil_moisture - BDSNP_SM_THRESH1) &
                / BDSNP_SM_RANGE2
        end if
    end function bdsnp_moisture_factor

    !> @brief BDSNP soil NO Fortran kernel.
    !>
    !> Implements both YL95 and BDSNP algorithms for soil NO emissions.
    !> Algorithm is selected via the soil_no_method parameter.
    !>
    !> @param soil_temp     Soil temperature [K]
    !> @param soil_moisture Soil moisture fraction [0-1]
    !> @param soil_nox      Output soil NO emissions [kg/m²/s] (accumulated)
    !> @param nx            Grid x-dimension
    !> @param ny            Grid y-dimension
    !> @param nz            Grid z-dimension
    !> @param soil_no_method Algorithm: 0 = BDSNP, 1 = YL95
    subroutine run_bdsnp_fortran(soil_temp, soil_moisture, soil_nox, &
                                 nx, ny, nz, soil_no_method) &
        bind(C, name="run_bdsnp_fortran")

        integer(c_int), value, intent(in) :: nx, ny, nz
        integer(c_int), value, intent(in) :: soil_no_method

        real(c_double), intent(in)    :: soil_temp(nx, ny, nz)
        real(c_double), intent(in)    :: soil_moisture(nx, ny, nz)
        real(c_double), intent(inout) :: soil_nox(nx, ny, nz)

        ! Local variables
        integer :: i, j, k
        real(c_double) :: tc, gw, sm
        real(c_double) :: t_term, w_term, sm_factor, t_response
        real(c_double) :: emiss

        do k = 1, nz
            do j = 1, ny
                do i = 1, nx
                    tc = soil_temp(i, j, k) - 273.15d0

                    ! Both modes: zero emission below freezing
                    if (tc <= FREEZING_C) then
                        cycle
                    end if

                    if (soil_no_method == 1) then
                        ! ================================================
                        ! YL95 mode: temperature + moisture response
                        ! ================================================
                        gw = soil_moisture(i, j, k)
                        t_term = soil_temp_term_yl95(tc)
                        w_term = soil_wet_term_yl95(gw)
                        emiss = A_BIOME_WET * UNITCONV * t_term * w_term
                        soil_nox(i, j, k) = soil_nox(i, j, k) + emiss
                    else
                        ! ================================================
                        ! BDSNP mode: uses BDSNP moisture factor
                        ! ================================================
                        ! Since the Fortran bridge only passes soil_temp,
                        ! soil_moisture, and soil_nox, the BDSNP mode uses
                        ! the same temperature response as YL95 but with
                        ! the BDSNP piecewise-linear moisture factor.
                        ! Additional BDSNP fields (N-dep, LAI, biome_ef)
                        ! default to neutral values (matching C++ defaults):
                        !   base_ef = 1.0, fert_factor = 1.0,
                        !   canopy_red = 1.0, pulse = 1.0
                        sm = soil_moisture(i, j, k)

                        ! Temperature response: exp(0.103 * min(30, tc))
                        t_response = exp(EXP_COEFF * min(TC_MAX, tc))

                        ! BDSNP piecewise-linear moisture factor
                        sm_factor = bdsnp_moisture_factor(sm)

                        ! Total BDSNP emission [kg NO/m2/s]
                        ! base_ef(1.0) * UNITCONV * t_response * sm_factor
                        !   * fert_factor(1.0) * canopy_red(1.0) * pulse(1.0)
                        emiss = BDSNP_BASE_EF * UNITCONV * t_response * sm_factor
                        soil_nox(i, j, k) = soil_nox(i, j, k) + emiss
                    end if
                end do
            end do
        end do

    end subroutine run_bdsnp_fortran

end module bdsnp_kernel_mod
