!> @file k14_kernel.F90
!> @brief Standalone K14 (Kok et al., 2014) dust emission kernel with bind(C) interface.
!>
!> This module provides a C-callable entry point for the K14 dust
!> emission computation, following the same bridge pattern as fengsha_kernel.F90.
!> The algorithm implements threshold friction velocity (Shao), gravimetric
!> soil moisture conversion (Zender), Fécan moisture correction, clay
!> parameterization (opt_clay), MacKinnon drag partition, Laurent erodibility,
!> IGBP vegetation masking, and Kok (2012, 2014) vertical dust flux.
!>
!> @author CECE Development Team
!> @date 2025
!> @version 1.0
module k14_kernel_mod
    use iso_c_binding
    implicit none

    ! Dc_soil lookup table: coarsest mode particle sizes for 12 STATSGO/FAO soil types [m]
    real(c_double), parameter :: Dc_soil(12) = (/ &
        710.0d-6, 710.0d-6, 125.0d-6, 125.0d-6, 125.0d-6, 160.0d-6, &
        710.0d-6, 125.0d-6, 125.0d-6, 160.0d-6, 125.0d-6,   2.0d-6 /)

    ! Shao threshold parameters
    real(c_double), parameter :: a_n = 0.0123d0
    real(c_double), parameter :: Dp_size = 75.0d-6   ! optimal saltation particle size [m]
    real(c_double), parameter :: rho_p = 2.65d3       ! soil particle density [kg/m³]

    ! Soil moisture conversion parameters
    real(c_double), parameter :: rho_water = 1000.0d0  ! water density [kg/m³]
    real(c_double), parameter :: rho_soil  = 2500.0d0  ! soil particle density [kg/m³]

    ! Roughness parameters
    real(c_double), parameter :: z0_valid = 0.08d-2    ! valid z0 range limit [m]
    real(c_double), parameter :: z0_max   = 6.25d0 * z0_valid  ! maximum roughness [m] = 5.0e-4

    ! Kok (2012, 2014) vertical flux parameters
    real(c_double), parameter :: rho_a0 = 1.225d0      ! standard air density [kg/m³]
    real(c_double), parameter :: u_st0  = 0.16d0       ! minimal u* for optimal erodibility [m/s]
    real(c_double), parameter :: C_d0   = 4.4d-5       ! dust emission coefficient
    real(c_double), parameter :: C_e    = 2.0d0        ! exponential decay parameter
    real(c_double), parameter :: C_a    = 2.7d0        ! power law exponent

contains

    !> @brief Compute K14 dust emissions (C-callable entry point).
    !>
    !> Converts C pointers to Fortran arrays and executes the complete K14
    !> dust emission algorithm over the 2D grid, including threshold friction
    !> velocity, soil moisture correction, clay parameterization, drag partition,
    !> erodibility, vegetation masking, and Kok vertical dust flux.
    !>
    !> @param[in]  t_soil_ptr     Soil temperature pointer [K]
    !> @param[in]  w_top_ptr      Volumetric soil moisture pointer [1]
    !> @param[in]  rho_air_ptr    Air density pointer [kg/m³]
    !> @param[in]  z0_ptr         Aeolian roughness length pointer [m]
    !> @param[in]  z_ptr          Height at wind level pointer [m]
    !> @param[in]  u_z_ptr        Eastward wind pointer [m/s]
    !> @param[in]  v_z_ptr        Northward wind pointer [m/s]
    !> @param[in]  ustar_ptr      Friction velocity pointer [m/s]
    !> @param[in]  f_land_ptr     Land fraction pointer [1]
    !> @param[in]  f_snow_ptr     Snow fraction pointer [1]
    !> @param[in]  f_src_ptr      Dust source potential pointer [1]
    !> @param[in]  f_sand_ptr     Sand fraction pointer [1]
    !> @param[in]  f_silt_ptr     Silt fraction pointer [1]
    !> @param[in]  f_clay_ptr     Clay fraction pointer [1]
    !> @param[in]  texture_ptr    Soil texture index pointer [1]
    !> @param[in]  vegetation_ptr IGBP vegetation category pointer [1]
    !> @param[in]  gvf_ptr        Vegetation fraction pointer [1]
    !> @param[in]  emissions_ptr  Output: dust emissions pointer [kg/(m²·s)]
    !> @param[in]  nx             Grid x-dimension
    !> @param[in]  ny             Grid y-dimension
    !> @param[in]  nbins          Number of dust size bins
    !> @param[in]  km             Model levels
    !> @param[in]  f_w            Soil moisture scaling factor
    !> @param[in]  f_c            Clay fraction scaling factor
    !> @param[in]  uts_gamma      Threshold velocity parameter gamma
    !> @param[in]  UNDEF_val      Undefined value sentinel
    !> @param[in]  GRAV           Gravity [m/s²]
    !> @param[in]  VON_KARMAN     Von Kármán constant
    !> @param[in]  opt_clay       Clay parameterization option (0, 1, 2)
    !> @param[in]  Ch_DU          Dust emission tuning coefficient
    subroutine run_k14_fortran( &
        t_soil_ptr, w_top_ptr, rho_air_ptr, z0_ptr, z_ptr, &
        u_z_ptr, v_z_ptr, ustar_ptr, f_land_ptr, f_snow_ptr, &
        f_src_ptr, f_sand_ptr, f_silt_ptr, f_clay_ptr, &
        texture_ptr, vegetation_ptr, gvf_ptr, emissions_ptr, &
        nx, ny, nbins, km, &
        f_w, f_c, uts_gamma, UNDEF_val, GRAV, VON_KARMAN, &
        opt_clay, Ch_DU &
    ) bind(c, name="run_k14_fortran")

        ! Pointer arguments
        type(c_ptr), value :: t_soil_ptr, w_top_ptr, rho_air_ptr
        type(c_ptr), value :: z0_ptr, z_ptr, u_z_ptr, v_z_ptr
        type(c_ptr), value :: ustar_ptr, f_land_ptr, f_snow_ptr
        type(c_ptr), value :: f_src_ptr, f_sand_ptr, f_silt_ptr, f_clay_ptr
        type(c_ptr), value :: texture_ptr, vegetation_ptr, gvf_ptr
        type(c_ptr), value :: emissions_ptr

        ! Scalar arguments
        integer(c_int), value :: nx, ny, nbins, km
        real(c_double), value :: f_w, f_c, uts_gamma
        real(c_double), value :: UNDEF_val, GRAV, VON_KARMAN
        integer(c_int), value :: opt_clay
        real(c_double), value :: Ch_DU

        ! Local Fortran array pointers (from C pointers)
        real(c_double), pointer :: t_soil(:,:), w_top(:,:), rho_air(:,:)
        real(c_double), pointer :: z0(:,:), z(:,:), u_z(:,:), v_z(:,:)
        real(c_double), pointer :: ustar(:,:), f_land(:,:), f_snow(:,:)
        real(c_double), pointer :: f_src(:,:), f_sand(:,:), f_silt(:,:), f_clay(:,:)
        real(c_double), pointer :: texture(:,:), vegetation(:,:), gvf(:,:)
        real(c_double), pointer :: emissions(:,:,:)

        ! Local working arrays
        real(c_double), allocatable :: u_ts(:,:)      ! threshold friction velocity over smooth surface
        real(c_double), allocatable :: w_g(:,:)       ! gravimetric soil moisture
        real(c_double), allocatable :: w_gt(:,:)      ! threshold gravimetric soil moisture
        real(c_double), allocatable :: H_w(:,:)       ! soil moisture correction
        real(c_double), allocatable :: clay(:,:)      ! corrected clay fraction
        real(c_double), allocatable :: silt(:,:)      ! corrected silt fraction
        real(c_double), allocatable :: k_gamma(:,:)   ! clay/silt modulation term
        real(c_double), allocatable :: z0s(:,:)       ! smooth roughness length
        real(c_double), allocatable :: R(:,:)         ! drag partition correction
        real(c_double), allocatable :: u(:,:)         ! soil friction velocity
        real(c_double), allocatable :: u_t(:,:)       ! soil threshold friction velocity
        real(c_double), allocatable :: f_erod(:,:)    ! erodibility
        real(c_double), allocatable :: f_veg(:,:)     ! vegetation mask
        real(c_double), allocatable :: flux(:,:)      ! vertical dust flux

        ! Local scalars
        integer :: i, j, n
        real(c_double) :: u_st_local, f_ust_local, C_d_local

        ! Convert C pointers to Fortran arrays
        call c_f_pointer(t_soil_ptr, t_soil, [nx, ny])
        call c_f_pointer(w_top_ptr, w_top, [nx, ny])
        call c_f_pointer(rho_air_ptr, rho_air, [nx, ny])
        call c_f_pointer(z0_ptr, z0, [nx, ny])
        call c_f_pointer(z_ptr, z, [nx, ny])
        call c_f_pointer(u_z_ptr, u_z, [nx, ny])
        call c_f_pointer(v_z_ptr, v_z, [nx, ny])
        call c_f_pointer(ustar_ptr, ustar, [nx, ny])
        call c_f_pointer(f_land_ptr, f_land, [nx, ny])
        call c_f_pointer(f_snow_ptr, f_snow, [nx, ny])
        call c_f_pointer(f_src_ptr, f_src, [nx, ny])
        call c_f_pointer(f_sand_ptr, f_sand, [nx, ny])
        call c_f_pointer(f_silt_ptr, f_silt, [nx, ny])
        call c_f_pointer(f_clay_ptr, f_clay, [nx, ny])
        call c_f_pointer(texture_ptr, texture, [nx, ny])
        call c_f_pointer(vegetation_ptr, vegetation, [nx, ny])
        call c_f_pointer(gvf_ptr, gvf, [nx, ny])
        call c_f_pointer(emissions_ptr, emissions, [nx, ny, nbins])

        ! Allocate working arrays
        allocate(u_ts(nx, ny), w_g(nx, ny), w_gt(nx, ny), source=UNDEF_val)
        allocate(H_w(nx, ny), source=1.0d0)
        allocate(clay(nx, ny), silt(nx, ny), k_gamma(nx, ny), source=UNDEF_val)
        allocate(z0s(nx, ny), source=125.0d-6)
        allocate(R(nx, ny), source=1.0d0)
        allocate(u(nx, ny), u_t(nx, ny), source=UNDEF_val)
        allocate(f_erod(nx, ny), source=UNDEF_val)
        allocate(f_veg(nx, ny), source=0.0d0)
        allocate(flux(nx, ny), source=0.0d0)

        ! Initialize emissions to zero
        emissions = 0.0d0

        ! ---------------------------------------------------------------
        ! Step 1: Threshold friction velocity over smooth surface (Shao)
        ! ---------------------------------------------------------------
        where ((f_land > 0.0d0) .and. (z0 < z0_max) .and. (z0 > 0.0d0))
            u_ts = sqrt(a_n * (((rho_p / rho_air) * GRAV * Dp_size) &
                   + uts_gamma / (rho_air * Dp_size)))
        end where

        ! ---------------------------------------------------------------
        ! Step 2: Gravimetric soil moisture (Zender formulation)
        ! ---------------------------------------------------------------
        w_g = UNDEF_val
        where (f_land > 0.0d0)
            w_g = 100.0d0 * f_w * rho_water / rho_soil &
                  / (1.0d0 - (0.489d0 - 0.126d0 * f_sand)) * w_top
        end where

        ! ---------------------------------------------------------------
        ! Step 3: Fécan soil moisture correction
        ! ---------------------------------------------------------------
        clay = UNDEF_val
        silt = UNDEF_val
        w_gt = UNDEF_val
        where ((f_land > 0.0d0) .and. (f_clay <= 1.0d0) .and. (f_clay >= 0.0d0))
            clay = f_c * f_clay
            silt = f_silt + (1.0d0 - f_c) * f_clay
            w_gt = 14.0d0 * clay * clay + 17.0d0 * clay
        end where

        H_w = 1.0d0
        where ((f_land > 0.0d0) .and. (w_g > w_gt))
            H_w = sqrt(1.0d0 + 1.21d0 * (w_g - w_gt)**0.68d0)
        end where

        ! ---------------------------------------------------------------
        ! Step 4: Clay parameterization (opt_clay)
        ! ---------------------------------------------------------------
        select case (opt_clay)
        case (1)
            ! Ito and Kok, 2017 variant 1
            k_gamma = 0.05d0

            where ((f_land > 0.0d0) .and. (clay < 0.2d0) .and. (clay >= 0.05d0))
                k_gamma = clay
            end where

            where ((f_land > 0.0d0) .and. (clay >= 0.2d0) .and. (clay <= 1.0d0))
                k_gamma = 0.2d0
            end where
        case (2)
            ! Ito and Kok, 2017 variant 2
            k_gamma = 1.0d0 / 1.4d0

            where ((f_land > 0.0d0) .and. (clay < 0.2d0) .and. (clay >= 0.0d0))
                k_gamma = 1.0d0 / (1.4d0 - clay - silt)
            end where

            where ((f_land > 0.0d0) .and. (clay >= 0.2d0) .and. (clay <= 1.0d0))
                k_gamma = 1.0d0 / (1.0d0 + clay - silt)
            end where
        case default
            ! Kok et al., 2014
            k_gamma = 0.0d0

            where ((f_land > 0.0d0) .and. (clay <= 1.0d0) .and. (clay >= 0.0d0))
                k_gamma = clay
            end where
        end select

        ! ---------------------------------------------------------------
        ! Step 5: Smooth roughness length from soil texture
        ! ---------------------------------------------------------------
        z0s = 125.0d-6
        do j = 1, ny
            do i = 1, nx
                if (texture(i,j) > 0.0d0 .and. texture(i,j) < 13.0d0) then
                    z0s(i,j) = Dc_soil(nint(texture(i,j)))
                end if
            end do
        end do
        z0s = z0s / 30.0d0

        ! ---------------------------------------------------------------
        ! Step 6: MacKinnon drag partition
        ! ---------------------------------------------------------------
        R = 1.0d0
        where ((f_land > 0.0d0) .and. (z0 < z0_max) .and. (z0 > z0s))
            R = 1.0d0 - log(z0 / z0s) / log(0.7d0 * (122.55d0 / z0s)**0.8d0)
        end where

        ! ---------------------------------------------------------------
        ! Step 7: Soil friction velocity
        ! ---------------------------------------------------------------
        u = UNDEF_val
        where ((f_land > 0.0d0) .and. (z0 < z0_max) .and. (z0 > 0.0d0))
            u = R * ustar
        end where

        ! ---------------------------------------------------------------
        ! Step 8: Soil threshold friction velocity
        ! ---------------------------------------------------------------
        u_t = UNDEF_val
        where ((f_land > 0.0d0) .and. (z0 < z0_max) .and. (z0 > 0.0d0))
            u_t = u_ts * H_w
        end where

        ! ---------------------------------------------------------------
        ! Step 9: Laurent erodibility
        ! ---------------------------------------------------------------
        f_erod = UNDEF_val
        where (f_land > 0.0d0)
            f_erod = 1.0d0
        end where

        where ((f_land > 0.0d0) .and. (z0 > 3.0d-5) .and. (z0 < z0_max))
            f_erod = 0.7304d0 - 0.0804d0 * log10(100.0d0 * z0)
        end where

        ! bedrock
        where (abs(texture - 15.0d0) < 0.5d0) f_erod = 0.0d0

        ! ---------------------------------------------------------------
        ! Step 10: IGBP vegetation mask
        ! ---------------------------------------------------------------
        f_veg = 0.0d0
        where ((f_land > 0.0d0) .and. abs(vegetation - 7.0d0) < 0.1d0)  f_veg = 1.0d0   ! open shrublands
        where ((f_land > 0.0d0) .and. abs(vegetation - 16.0d0) < 0.1d0) f_veg = 1.0d0   ! barren or sparsely vegetated

        ! vegetation mask: modulate with vegetation fraction
        where (f_land > 0.0d0 .and. gvf >= 0.0d0 .and. gvf < 0.8d0) f_veg = f_veg * (1.0d0 - gvf)

        ! ---------------------------------------------------------------
        ! Step 11: Final erodibility
        ! ---------------------------------------------------------------
        f_erod = f_erod * f_veg * f_land * (1.0d0 - f_snow)

        ! kludge to deal with high emissions in Australia
        where (f_src >= 0.0d0) f_erod = f_src * f_erod

        ! ---------------------------------------------------------------
        ! Step 12: Kok vertical dust flux
        ! ---------------------------------------------------------------
        flux = 0.0d0
        do j = 1, ny
            do i = 1, nx
                if ((f_erod(i,j) > 0.0d0) .and. (u(i,j) > u_t(i,j))) then
                    u_st_local = u_t(i,j) * sqrt(rho_air(i,j) / rho_a0)
                    u_st_local = max(u_st_local, u_st0)

                    f_ust_local = (u_st_local - u_st0) / u_st0
                    C_d_local = C_d0 * exp(-C_e * f_ust_local)

                    flux(i,j) = C_d_local * f_erod(i,j) * k_gamma(i,j) * rho_air(i,j) &
                                * ((u(i,j) * u(i,j) - u_t(i,j) * u_t(i,j)) / u_st_local) &
                                * (u(i,j) / u_t(i,j))**(C_a * f_ust_local)
                end if
            end do
        end do

        ! ---------------------------------------------------------------
        ! Step 13: Scale and replicate across bins
        ! ---------------------------------------------------------------
        emissions(:,:,1) = flux * Ch_DU
        do n = 2, nbins
            emissions(:,:,n) = emissions(:,:,1)
        end do

        ! Deallocate working arrays
        deallocate(u_ts, w_g, w_gt, H_w, clay, silt, k_gamma, z0s, R, u, u_t, f_erod, f_veg, flux)

    end subroutine run_k14_fortran

end module k14_kernel_mod
