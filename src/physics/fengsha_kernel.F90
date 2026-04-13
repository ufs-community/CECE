!> @file fengsha_kernel.F90
!> @brief Standalone FENGSHA dust emission kernel with bind(C) interface.
!>
!> This module provides a C-callable entry point for the FENGSHA dust
!> emission computation, following the same bridge pattern as dust_kernel.F90.
!>
!> @author CECE Development Team
!> @date 2025
!> @version 1.0
module fengsha_kernel_mod
    use iso_c_binding
    implicit none

    ! Constants
    integer, parameter :: LAND = 1
    real(c_double), parameter :: SSM_THRESH = 1.0d-2
    real(c_double), parameter :: RHOW = 1000.0d0
    real(c_double), parameter :: RHOP = 1700.0d0
    real(c_double), parameter :: CLAY_THRESH = 0.2d0

contains

    !> @brief Convert volumetric to gravimetric soil moisture.
    !> @param[in] vsoil    Volumetric soil moisture fraction [1]
    !> @param[in] sandfrac Fractional sand content [1]
    !> @return Gravimetric soil moisture [percent]
    pure function soil_moisture_vol2grav(vsoil, sandfrac) result(grav_moisture)
        real(c_double), intent(in) :: vsoil, sandfrac
        real(c_double) :: grav_moisture
        real(c_double) :: vsat

        vsat = 0.489d0 - 0.126d0 * sandfrac
        grav_moisture = 100.0d0 * vsoil * RHOW / (RHOP * (1.0d0 - vsat))
    end function soil_moisture_vol2grav

    !> @brief Compute Fecan soil moisture correction factor.
    !> @param[in] slc  Liquid water content, volumetric fraction [1]
    !> @param[in] sand Fractional sand content [1]
    !> @param[in] clay Fractional clay content [1]
    !> @param[in] b    Drylimit factor [1]
    !> @return Soil moisture correction factor [1]
    pure function moisture_correction_fecan(slc, sand, clay, b) result(correction)
        real(c_double), intent(in) :: slc, sand, clay, b
        real(c_double) :: correction
        real(c_double) :: grvsoilm, drylimit, excess

        grvsoilm = soil_moisture_vol2grav(slc, sand)
        drylimit = b * clay * (14.0d0 * clay + 17.0d0)
        excess = max(0.0d0, grvsoilm - drylimit)
        correction = sqrt(1.0d0 + 1.21d0 * excess**0.68d0)
    end function moisture_correction_fecan

    !> @brief Compute vertical-to-horizontal dust flux ratio (MB95).
    !> @param[in] clay   Fractional clay content [1]
    !> @param[in] kvhmax Maximum flux ratio [1]
    !> @return Vertical-to-horizontal dust flux ratio [1]
    pure function flux_v2h_ratio_mb95(clay, kvhmax) result(ratio)
        real(c_double), intent(in) :: clay, kvhmax
        real(c_double) :: ratio

        if (clay > CLAY_THRESH) then
            ratio = kvhmax
        else
            ratio = 10.0d0**(13.4d0 * clay - 6.0d0)
        end if
    end function flux_v2h_ratio_mb95

    !> @brief Compute FENGSHA dust emissions (C-callable entry point).
    !>
    !> Converts C pointers to Fortran arrays and executes the FENGSHA
    !> dust emission algorithm over the 2D grid.
    !>
    !> @param[in]  ustar_ptr       Friction velocity pointer [m/s]
    !> @param[in]  uthrs_ptr       Threshold velocity pointer [m/s]
    !> @param[in]  slc_ptr         Soil liquid water content pointer [1]
    !> @param[in]  clay_ptr        Clay fraction pointer [1]
    !> @param[in]  sand_ptr        Sand fraction pointer [1]
    !> @param[in]  silt_ptr        Silt fraction pointer [1]
    !> @param[in]  ssm_ptr         Erodibility map pointer [1]
    !> @param[in]  rdrag_ptr       Drag partition pointer [1/m]
    !> @param[in]  airdens_ptr     Air density pointer [kg/m^3]
    !> @param[in]  fraclake_ptr    Lake fraction pointer [1]
    !> @param[in]  fracsnow_ptr    Snow fraction pointer [1]
    !> @param[in]  oro_ptr         Land-ocean-ice mask pointer [1]
    !> @param[in]  emissions_ptr   Output: binned emissions pointer [kg/(m^2 s)]
    !> @param[in]  nx              Grid x-dimension
    !> @param[in]  ny              Grid y-dimension
    !> @param[in]  nbins           Number of dust size bins
    !> @param[in]  alpha           Horizontal flux scaling factor [1]
    !> @param[in]  gamma_param     Erodibility exponent [1]
    !> @param[in]  kvhmax          Max V/H flux ratio [1]
    !> @param[in]  grav            Gravity [m/s^2]
    !> @param[in]  drylimit_factor Fecan drylimit tuning factor [1]
    subroutine run_fengsha_fortran( &
        ustar_ptr, uthrs_ptr, slc_ptr, clay_ptr, sand_ptr, silt_ptr, &
        ssm_ptr, rdrag_ptr, airdens_ptr, fraclake_ptr, fracsnow_ptr, &
        oro_ptr, emissions_ptr, &
        nx, ny, nbins, &
        alpha, gamma_param, kvhmax, grav, drylimit_factor &
    ) bind(c, name="run_fengsha_fortran")

        ! Pointer arguments
        type(c_ptr), value :: ustar_ptr, uthrs_ptr, slc_ptr
        type(c_ptr), value :: clay_ptr, sand_ptr, silt_ptr
        type(c_ptr), value :: ssm_ptr, rdrag_ptr, airdens_ptr
        type(c_ptr), value :: fraclake_ptr, fracsnow_ptr, oro_ptr
        type(c_ptr), value :: emissions_ptr

        ! Scalar arguments
        integer(c_int), value :: nx, ny, nbins
        real(c_double), value :: alpha, gamma_param, kvhmax, grav, drylimit_factor

        ! Local Fortran array pointers
        real(c_double), pointer :: ustar(:,:), uthrs(:,:), slc(:,:)
        real(c_double), pointer :: clay(:,:), sand(:,:), silt(:,:)
        real(c_double), pointer :: ssm(:,:), rdrag(:,:), airdens(:,:)
        real(c_double), pointer :: fraclake(:,:), fracsnow(:,:), oro(:,:)
        real(c_double), pointer :: emissions(:,:,:)

        ! Local variables
        integer :: i, j, n
        real(c_double) :: alpha_grav, fracland, kvh, total_emissions
        real(c_double) :: rustar, smois, h, u_thresh, u_sum, q
        logical :: skip

        ! Hard-coded Kok distribution for 5 bins (default)
        ! These match the normalized distribution from DustAerosolDistributionKok
        real(c_double) :: distribution(5)
        distribution = (/ 0.1d0, 0.25d0, 0.25d0, 0.25d0, 0.15d0 /)

        ! Convert C pointers to Fortran arrays
        call c_f_pointer(ustar_ptr, ustar, [nx, ny])
        call c_f_pointer(uthrs_ptr, uthrs, [nx, ny])
        call c_f_pointer(slc_ptr, slc, [nx, ny])
        call c_f_pointer(clay_ptr, clay, [nx, ny])
        call c_f_pointer(sand_ptr, sand, [nx, ny])
        call c_f_pointer(silt_ptr, silt, [nx, ny])
        call c_f_pointer(ssm_ptr, ssm, [nx, ny])
        call c_f_pointer(rdrag_ptr, rdrag, [nx, ny])
        call c_f_pointer(airdens_ptr, airdens, [nx, ny])
        call c_f_pointer(fraclake_ptr, fraclake, [nx, ny])
        call c_f_pointer(fracsnow_ptr, fracsnow, [nx, ny])
        call c_f_pointer(oro_ptr, oro, [nx, ny])
        call c_f_pointer(emissions_ptr, emissions, [nx, ny, nbins])

        ! Initialize emissions to zero
        emissions = 0.0d0

        ! Prepare scaling factor
        alpha_grav = alpha / grav

        ! Main computation loop
        do j = 1, ny
            do i = 1, nx
                ! Skip if not on land
                skip = (nint(oro(i,j)) /= LAND)

                ! Threshold and sanity checks
                if (.not. skip) skip = (ssm(i,j) < SSM_THRESH) &
                    .or. (clay(i,j) < 0.0d0) .or. (sand(i,j) < 0.0d0) &
                    .or. (rdrag(i,j) < 0.0d0)

                if (.not. skip) then
                    fracland = max(0.0d0, min(1.0d0, 1.0d0 - fraclake(i,j))) &
                             * max(0.0d0, min(1.0d0, 1.0d0 - fracsnow(i,j)))

                    ! Vertical-to-horizontal mass flux ratio
                    kvh = flux_v2h_ratio_mb95(clay(i,j), kvhmax)

                    ! Total emissions scaling
                    total_emissions = alpha_grav * fracland * (ssm(i,j) ** gamma_param) &
                                    * airdens(i,j) * kvh

                    ! Drag-partition-adjusted friction velocity
                    rustar = rdrag(i,j) * ustar(i,j)

                    ! Fecan moisture correction
                    smois = slc(i,j)
                    h = moisture_correction_fecan(smois, sand(i,j), clay(i,j), drylimit_factor)

                    ! Adjusted threshold
                    u_thresh = uthrs(i,j) * h
                    u_sum = rustar + u_thresh

                    ! Horizontal saltation flux (Webb et al. 2020, Eq. 9)
                    q = max(0.0d0, rustar - u_thresh) * u_sum * u_sum

                    ! Distribute to bins
                    do n = 1, min(nbins, 5)
                        emissions(i,j,n) = distribution(n) * total_emissions * q
                    end do
                end if
            end do
        end do

    end subroutine run_fengsha_fortran

end module fengsha_kernel_mod
