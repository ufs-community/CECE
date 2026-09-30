!> @file geos12_seasalt_kernel.F90
!> @brief Fortran kernel for the GEOS-12 (GEOS-Chem 2012) sea salt emission scheme.
!>
!> Ported from CATChem SeaSaltScheme_GEOS12_Mod.F90 (Jaeglé et al. 2011 SST
!> correction, Gong 2003 source function, optional Fan & Toon 2011 Weibull
!> wind correction). Infrastructure-free science kernel exposed to C++ via a
!> bind(C) entry point, following the same bridge pattern as bdsnp_kernel.F90.
!>
!> Per-bin size properties (density, lower/upper dry radius) are supplied by the
!> host as flat arrays; met inputs are surface fields flattened as (nx, ny).
!> The third dimension of the emission outputs indexes the size bin (species).
!>
!> @author CECE Team
!> @date 2026
module geos12_seasalt_kernel_mod
    use iso_c_binding
    implicit none
    private

    public :: run_seasalt_geos12_fortran

    ! Fixed GEOS-12 Gong (2003) source-function parameters
    real(c_double), parameter :: SCALEFAC = 33.0d3
    real(c_double), parameter :: RPOW = 3.45d0
    real(c_double), parameter :: EXPPOW = 1.607d0
    real(c_double), parameter :: WPOW = 3.41d0 - 1.0d0

    ! Sub-bin integration controls
    integer(c_int), parameter :: NR = 10           ! linear dry sub-bins
    real(c_double), parameter :: R80FAC = 1.65d0   ! r(RH=0.8)/r(dry) [Gerber]

contains

    !> @brief Compute GEOS-12 sea salt mass and number emissions (C-callable).
    !>
    !> @param[in]  frocean       Ocean fraction [1] (nx, ny)
    !> @param[in]  frseaice      Sea-ice fraction [1] (nx, ny)
    !> @param[in]  lat           Latitude [deg] (nx, ny)
    !> @param[in]  lon           Longitude [deg] (nx, ny)
    !> @param[in]  sst           Sea surface temperature [K] (nx, ny)
    !> @param[in]  u10m          10-m eastward wind [m/s] (nx, ny)
    !> @param[in]  v10m          10-m northward wind [m/s] (nx, ny)
    !> @param[in]  ustar         Friction velocity [m/s] (nx, ny)
    !> @param[in]  density       Per-bin dry particle density [kg/m^3] (ns)
    !> @param[in]  r_low         Per-bin lower dry radius [um] (ns)
    !> @param[in]  r_up          Per-bin upper dry radius [um] (ns)
    !> @param[out] mass_emis     Mass emission flux [kg/m^2/s] (nx, ny, ns)
    !> @param[out] number_emis   Number emission flux [#/m^2/s] (nx, ny, ns)
    !> @param[in]  nx, ny, ns    Grid dimensions and bin count
    !> @param[in]  weibull_flag  Enable Weibull wind correction (0/1)
    !> @param[in]  scale_factor  Global tuning scale factor [1]
    !> @param[in]  pi            Value of pi
    subroutine run_seasalt_geos12_fortran( &
        frocean, frseaice, lat, lon, sst, u10m, v10m, ustar, &
        density, r_low, r_up, mass_emis, number_emis, &
        nx, ny, ns, weibull_flag, scale_factor, pi) &
        bind(C, name="run_seasalt_geos12_fortran")

        integer(c_int), value, intent(in) :: nx, ny, ns
        integer(c_int), value, intent(in) :: weibull_flag
        real(c_double), value, intent(in) :: scale_factor, pi

        real(c_double), intent(in) :: frocean(nx, ny), frseaice(nx, ny)
        real(c_double), intent(in) :: lat(nx, ny), lon(nx, ny), sst(nx, ny)
        real(c_double), intent(in) :: u10m(nx, ny), v10m(nx, ny), ustar(nx, ny)
        real(c_double), intent(in) :: density(ns), r_low(ns), r_up(ns)
        real(c_double), intent(inout) :: mass_emis(nx, ny, ns)
        real(c_double), intent(inout) :: number_emis(nx, ny, ns)

        integer(c_int) :: i, j, n, ir, rc
        logical :: do_weibull
        real(c_double) :: w10m, gweibull, fsstemis, deep_lakes_mask, dummylon
        real(c_double) :: scale, ocean_frac
        real(c_double) :: dry_radius, delta_dry, rwet, drwet
        real(c_double) :: afac, bfac, mass_scale
        real(c_double) :: mass_acc, number_acc

        do_weibull = (weibull_flag /= 0)

        mass_emis = 0.0d0
        number_emis = 0.0d0

        do j = 1, ny
            do i = 1, nx
                ! Skip cells with no open ocean
                ocean_frac = frocean(i, j) - frseaice(i, j)
                if (ocean_frac <= 0.0d0) cycle

                ! 10-m mean wind speed (used only for the Weibull correction)
                w10m = sqrt(u10m(i, j)**2 + v10m(i, j)**2)

                call weibull_distribution(gweibull, do_weibull, w10m, rc)
                if (rc /= 0) cycle

                call jeagle_sst_correction(fsstemis, sst(i, j))

                ! Deep-lakes mask (Great Lakes and Caspian Sea)
                deep_lakes_mask = 1.0d0
                dummylon = lon(i, j)
                if (dummylon < 0.0d0) dummylon = dummylon + 360.0d0
                if (lat(i, j) >= 40.5d0 .and. lat(i, j) <= 50.0d0 .and. &
                    dummylon >= 267.0d0 .and. dummylon <= 285.0d0) deep_lakes_mask = 0.0d0
                if (lat(i, j) >= 35.0d0 .and. lat(i, j) <= 48.0d0 .and. &
                    dummylon >= 45.0d0 .and. dummylon <= 56.0d0) deep_lakes_mask = 0.0d0

                scale = min(max(0.0d0, ocean_frac * deep_lakes_mask), 1.0d0) &
                        * gweibull * fsstemis * scale_factor

                do n = 1, ns
                    delta_dry = (r_up(n) - r_low(n)) / real(NR, c_double)
                    dry_radius = r_low(n) + 0.5d0 * delta_dry

                    mass_acc = 0.0d0
                    number_acc = 0.0d0

                    do ir = 1, NR
                        rwet = R80FAC * dry_radius
                        drwet = R80FAC * delta_dry

                        afac = 4.7d0 * (1.0d0 + 30.0d0 * rwet)**(-0.017d0 * rwet**(-1.44d0))
                        bfac = (0.433d0 - log10(rwet)) / 0.433d0

                        mass_scale = SCALEFAC * 4.0d0 / 3.0d0 * pi * density(n) &
                                     * (dry_radius**3) * 1.0d-18

                        number_acc = number_acc + &
                            seasalt_emission_gong(rwet, drwet, ustar(i, j), SCALEFAC, afac, bfac)
                        mass_acc = mass_acc + &
                            seasalt_emission_gong(rwet, drwet, ustar(i, j), mass_scale, afac, bfac)

                        dry_radius = dry_radius + delta_dry
                    end do

                    mass_emis(i, j, n) = max(0.0d0, mass_acc * scale)
                    number_emis(i, j, n) = max(0.0d0, number_acc * scale)
                end do
            end do
        end do

    end subroutine run_seasalt_geos12_fortran

    !> @brief Gong (2003) size- and wind-dependent sea salt source function.
    pure function seasalt_emission_gong(r, dr, w, scalefac_in, afac, bfac) result(emis)
        real(c_double), intent(in) :: r, dr, w, scalefac_in, afac, bfac
        real(c_double) :: emis

        emis = scalefac_in * 1.373d0 * r**(-afac) * (1.0d0 + 0.057d0 * r**RPOW) &
               * 10.0d0**(EXPPOW * exp(-bfac**2)) * dr
        emis = w**WPOW * emis
    end function seasalt_emission_gong

    !> @brief Jaeglé et al. (2011) SST correction (temperature-range branch).
    pure subroutine jeagle_sst_correction(fsstemis, sst)
        real(c_double), intent(out) :: fsstemis
        real(c_double), intent(in) :: sst
        real(c_double) :: tskin_c

        tskin_c = sst - 273.15d0
        tskin_c = max(-0.1d0, tskin_c)
        tskin_c = min(36.0d0, tskin_c)
        fsstemis = (-1.107211d0 - 0.010681d0 * tskin_c - 0.002276d0 * tskin_c**2 &
                    + 60.288927d0 * 1.0d0 / (40.0d0 - tskin_c))
        fsstemis = max(0.0d0, fsstemis)
        fsstemis = min(7.0d0, fsstemis)
    end subroutine jeagle_sst_correction

    !> @brief Fan & Toon (2011) Weibull wind-speed correction factor.
    subroutine weibull_distribution(gweibull, weibull_flag, wm, rc)
        real(c_double), intent(out) :: gweibull
        logical, intent(in) :: weibull_flag
        real(c_double), intent(in) :: wm
        integer(c_int), intent(out) :: rc
        real(c_double) :: a, c, k, wt, x

        rc = 0
        gweibull = 1.0d0
        wt = 4.0d0

        if (weibull_flag) then
            gweibull = 0.0d0
            if (wm > 0.01d0) then
                k = 0.94d0 * sqrt(wm)
                c = wm / gamma(1.0d0 + 1.0d0 / k)
                x = (wt / c)**k
                a = 3.41d0 / k + 1.0d0
                gweibull = (c / wm)**3.41d0 * igamma(a, x, rc)
            end if
        end if
    end subroutine weibull_distribution

    !> @brief Upper incomplete Gamma function used by the Weibull correction.
    function igamma(a, x, rc) result(gout)
        real(c_double), intent(in) :: a, x
        integer(c_int), intent(out) :: rc
        real(c_double) :: gout
        real(c_double) :: xam, gin, s, r, t0
        integer(c_int) :: k

        rc = 0
        gout = 0.0d0
        xam = -x + a * log(x)
        if (xam > 700.0d0 .or. a > 170.0d0) then
            rc = -1
            return
        end if

        if (abs(x) < 1.0d-30) then
            gout = gamma(a)
        else if (x <= 1.0d0 + a) then
            s = 1.0d0 / a
            r = s
            do k = 1, 60
                r = r * x / (a + k)
                s = s + r
                if (abs(r / s) < 1.0d-15) exit
            end do
            gin = exp(xam) * s
            gout = gamma(a) - gin
        else
            t0 = 0.0d0
            do k = 60, 1, -1
                t0 = (k - a) / (1.0d0 + k / (x + t0))
            end do
            gout = exp(xam) / (x + t0)
        end if
    end function igamma

end module geos12_seasalt_kernel_mod
