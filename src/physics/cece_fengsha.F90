!
! !DESCRIPTION:
!
!  This module contains and implements all necessary process calculations for GOCART.
!
! !REVISION HISTORY:
!
!  11Feb2020  E.Sherman, A.da Silva, T.Clune, A.Darmenov - Ported/consolidated/refactored GOCART
!                   physics and chemistry code into a single process library that only uses
!                   intrinsic Fortran functions.
!
!  01Apr2021  R.Montuoro/NOAA - Added FENGSHA dust scheme and related methods.
!
!
!EOP
!-------------------------------------------------------------------------
CONTAINS

!=====================================================================================
!BOP
!
! !IROUTINE:  DustAerosolDistributionKok - Compute Kok's dust size aerosol distribution
!
! !INTERFACE:
   subroutine DustAerosolDistributionKok ( radius, rLow, rUp, distribution )

! !USES:
   implicit NONE

! !INPUT PARAMETERS:
   real, dimension(:), intent(in)  :: radius      ! Dry particle bin effective radius [um]
   real, dimension(:), intent(in)  :: rLow, rUp   ! Dry particle bin edge radii [um]

! !OUTPUT PARAMETERS:
   real, dimension(:), intent(out) :: distribution    ! Normalized dust aerosol distribution [1]

! !DESCRIPTION: Computes lognormal aerosol size distribution for dust bins according to
!               J.F.Kok, PNAS, Jan 2011, 108 (3) 1016-1021; doi:10.1073/pnas.1014798108
!
! !REVISION HISTORY:
!
! 22Feb2020 B.Baker/NOAA    - Original implementation
! 01Apr2021 R.Montuoro/NOAA - Refactored for GOCART process library
!

! !Local Variables
   integer :: n, nbins
   real    :: diameter, dlam, dvol

! !CONSTANTS
   real, parameter    :: mmd    = 3.4          ! median mass diameter [um]
   real, parameter    :: stddev = 3.0          ! geometric standard deviation [1]
   real, parameter    :: lambda = 12.0         ! crack propagation length [um]
   real, parameter    :: factor = 1.e0 / (sqrt(2.e0) * log(stddev))  ! auxiliary constant

   character(len=*), parameter :: myname = 'DustAerosolDistributionKok'

!EOP
!-------------------------------------------------------------------------
!  Begin...

   distribution = 0.

!  Assume all arrays are dimensioned consistently
   nbins = size(radius)

   dvol = 0.
   do n = 1, nbins
     diameter = 2 * radius(n)
     dlam = diameter/lambda
     distribution(n) = diameter * (1. + erf(factor * log(diameter/mmd))) &
                     * exp(-dlam * dlam * dlam) * log(rUp(n)/rLow(n))
     dvol = dvol + distribution(n)
   end do

!  Normalize distribution
   do n = 1, nbins
     distribution(n) = distribution(n) / dvol
   end do

   end subroutine DustAerosolDistributionKok

!===============================================================================
!BOP
!
! !IROUTINE: soilMoistureConvertVol2Grav - volumetric to gravimetric soil moisture
!
! !INTERFACE:
   real function soilMoistureConvertVol2Grav(vsoil, sandfrac)

! !USES:
   implicit NONE

! !INPUT PARAMETERS:
   real, intent(in) :: vsoil       ! volumetric soil moisture fraction [1]
   real, intent(in) :: sandfrac    ! fractional sand content [1]

! !DESCRIPTION: Convert soil moisture fraction from volumetric to gravimetric.
!
! !REVISION HISTORY:
!
!  02Apr2020, B.Baker/NOAA    - Original implementation
!  01Apr2020, R.Montuoro/NOAA - Adapted for GOCART process library

!  !Local Variables
   real :: vsat

!  !CONSTANTS:
   real, parameter :: rhow = 1000.    ! density of water [kg m-3]
   real, parameter :: rhop = 1700.
!EOP
!-------------------------------------------------------------------------
!  Begin...

!  Saturated volumetric water content (sand-dependent) ! [m3 m-3]
   vsat = 0.489 - 0.126 * sandfrac

!  Gravimetric soil content
   soilMoistureConvertVol2Grav = 100. * vsoil * rhow / (rhop * (1. - vsat))

   end function soilMoistureConvertVol2Grav

!===============================================================================
!BOP
!
! !IROUTINE: moistureCorrectionFecan - Correction factor for Fecan soil moisture
!
! !INTERFACE:
   real function moistureCorrectionFecan(slc, sand, clay, b)

! !USES:
   implicit NONE

! !INPUT PARAMETERS:
   real, intent(in) :: slc     ! liquid water content of top soil layer, volumetric fraction [1]
   real, intent(in) :: sand    ! fractional sand content [1]
   real, intent(in) :: clay    ! fractional clay content [1]
   real, intent(in) :: b       ! drylimit factor from zender 2003

! !DESCRIPTION: Compute correction factor to account for Fecal soil moisture
!
! !REVISION HISTORY:
!
!  02Apr2020, B.Baker/NOAA    - Original implementation
!  01Apr2020, R.Montuoro/NOAA - Adapted for GOCART process library

!  !Local Variables
   real :: grvsoilm
   real :: drylimit

!EOP
!-------------------------------------------------------------------------
!  Begin...

!  Convert soil moisture from volumetric to gravimetric
   grvsoilm = soilMoistureConvertVol2Grav(slc, sand)

!  Compute fecan dry limit
   drylimit = b * clay * (14.0 * clay + 17.0)

!  Compute soil moisture correction
   moistureCorrectionFecan = sqrt(1.0 + 1.21 * max(0., grvsoilm - drylimit)**0.68)

   end function moistureCorrectionFecan

!===============================================================================
!BOP
!
! !IROUTINE: DustFluxV2HRatioMB95 - vertical-to-horizontal dust flux ratio (MB95)
!
! !INTERFACE:
   real function DustFluxV2HRatioMB95(clay, kvhmax)

! !USES:
   implicit NONE

! !INPUT PARAMETERS:
   real, intent(in) :: clay      ! fractional clay content [1]
   real, intent(in) :: kvhmax    ! maximum flux ratio [1]

!  !CONSTANTS:
   real, parameter :: clay_thresh = 0.2    ! clay fraction above which the maximum flux ratio is returned

! !DESCRIPTION: Computes the vertical-to-horizontal dust flux ratio according to
!               B.Marticorena, G.Bergametti, J.Geophys.Res., 100(D8), 16415–16430, 1995
!               doi:10.1029/95JD00690
!
! !REVISION HISTORY:
!
! 22Feb2020 B.Baker/NOAA    - Original implementation
! 01Apr2021 R.Montuoro/NOAA - Adapted for GOCART process library
!
!EOP
!-------------------------------------------------------------------------
!  Begin...

   if (clay > clay_thresh) then
     DustFluxV2HRatioMB95 = kvhmax
   else
     DustFluxV2HRatioMB95 = 10.0**(13.4*clay-6.0)
   end if

   end function DustFluxV2HRatioMB95

!==================================================================================
!BOP
!
! !IROUTINE: DustEmissionFENGSHA - Compute dust emissions using NOAA/ARL FENGSHA model
!
! !INTERFACE:
   subroutine DustEmissionFENGSHA(fraclake, fracsnow, oro, slc, clay, sand, silt,  &
                                  ssm, rdrag, airdens, ustar, uthrs, alpha, gamma, &
                                  kvhmax, grav, rhop, distribution, drylimit_factor, moist_correct, emissions, rc)

! !USES:
   implicit NONE

! !INPUT PARAMETERS:
   real, dimension(:,:), intent(in) :: fraclake ! fraction of lake [1]
   real, dimension(:,:), intent(in) :: fracsnow ! surface snow area fraction [1]
   real, dimension(:,:), intent(in) :: slc      ! liquid water content of soil layer, volumetric fraction [1]
   real, dimension(:,:), intent(in) :: oro      ! land-ocean-ice mask [1]
   real, dimension(:,:), intent(in) :: clay     ! fractional clay content [1]
   real, dimension(:,:), intent(in) :: sand     ! fractional sand content [1]
   real, dimension(:,:), intent(in) :: silt     ! fractional silt content [1]
   real, dimension(:,:), intent(in) :: ssm      ! erosion map [1]
   real, dimension(:,:), intent(in) :: rdrag    ! drag partition [1/m]
   real, dimension(:,:), intent(in) :: airdens  ! air density at lowest level [kg/m^3]
   real, dimension(:,:), intent(in) :: ustar    ! friction velocity [m/sec]
   real, dimension(:,:), intent(in) :: uthrs    ! threshold velocity [m/2]
   real,                 intent(in) :: alpha    ! scaling factor [1]
   real,                 intent(in) :: gamma    ! scaling factor [1]
   real,                 intent(in) :: kvhmax   ! max. vertical to horizontal mass flux ratio [1]
   real,                 intent(in) :: grav     ! gravity [m/sec^2]
   real, dimension(:),   intent(in) :: rhop            ! soil class density [kg/m^3]
   real, dimension(:),   intent(in) :: distribution    ! normalized dust binned distribution [1]
   real,                 intent(in) :: drylimit_factor ! drylimit tuning factor from zender2003
   real,                 intent(in) :: moist_correct   ! moisture correction factor
! !OUTPUT PARAMETERS:
   real,    intent(out) :: emissions(:,:,:)     ! binned surface emissions [kg/(m^2 sec)]
   integer, intent(out) :: rc                   ! Error return code: __SUCCESS__ or __FAIL__

! !DESCRIPTION: Compute dust emissions using NOAA/ARL FENGSHA model
!
! !REVISION HISTORY:
!
! 22Feb2020 B.Baker/NOAA    - Original implementation
! 29Mar2021 R.Montuoro/NOAA - Refactored for process library
!

! !Local Variables
   logical               :: skip
   integer               :: i, j, n, nbins
   integer, dimension(2) :: ilb, iub
   real                  :: alpha_grav
   real                  :: fracland
   real                  :: h
   real                  :: kvh
   real                  :: q
   real                  :: rustar
   real                  :: total_emissions
   real                  :: u_sum, u_thresh
   real                  :: smois

! !CONSTANTS:
   real, parameter       :: ssm_thresh = 1.e-02    ! emit above this erodibility threshold [1]

!EOP
!-------------------------------------------------------------------------
!  Begin

   rc = __SUCCESS__

!  Get dimensions and index bounds
!  -------------------------------
   nbins = size(emissions, dim=3)
   ilb = lbound(ustar)
   iub = ubound(ustar)

!  Initialize emissions
!  --------------------
   emissions = 0.

!  Prepare scaling factor
!  ----------------------
   alpha_grav = alpha / grav

!  Compute size-independent factors for emission flux
!  ---------------------------
   do j = ilb(2), iub(2)
     do i = ilb(1), iub(1)
       ! skip if we are not on land
       ! --------------------------
       skip = (oro(i,j) /= LAND)
       ! threshold and sanity check for surface input
       ! --------------------------------------------
       if (.not.skip) skip = (ssm(i,j) < ssm_thresh) &
         .or. (clay(i,j) < 0.) .or. (sand(i,j) < 0.) &
         .or. (rdrag(i,j) < 0.)

       if (.not.skip) then
         fracland = max(0., min(1., 1.-fraclake(i,j))) &
                  * max(0., min(1., 1.-fracsnow(i,j)))

         ! Compute vertical-to-horizontal mass flux ratio
         ! ----------------------------------------------
         kvh = DustFluxV2HRatioMB95(clay(i,j), kvhmax)

         ! Compute total emissions
         ! -----------------------
         total_emissions = alpha_grav * fracland * (ssm(i,j) ** gamma) &
                         * airdens(i,j) * kvh

         !  Compute threshold wind friction velocity using drag partition
         !  -------------------------------------------------------------
         rustar = rdrag(i,j) * ustar(i,j)

         ! Fecan moisture correction
         ! -------------------------
         smois = slc(i,j) * moist_correct
         h = moistureCorrectionFecan(smois, sand(i,j), clay(i,j), drylimit_factor)

         ! Adjust threshold
         ! ----------------
         u_thresh = uthrs(i,j) * h

         u_sum = rustar + u_thresh

         ! Compute Horizontal Saltation Flux according to Eq (9) in Webb et al. (2020)
         ! ---------------------------------------------------------------------------
         q = max(0., rustar - u_thresh) * u_sum * u_sum

         !  Now compute size-dependent total emission flux
         !  ----------------------------------------------
         do n = 1, nbins
           ! Distribute emissions to bins and convert to mass flux (kg s-1)
           ! --------------------------------------------------------------
           emissions(i,j,n) = distribution(n) * total_emissions * q
         end do

       end if

     end do
   end do

   end subroutine DustEmissionFENGSHA

!==================================================================================
!BOP
! !IROUTINE: DustEmissionGOCART2G

   subroutine DustEmissionGOCART2G(radius, fraclake, gwettop, oro, u10m, &
                                   v10m, Ch_DU, du_src, grav, &
                                   emissions, rc )

! !USES:
   implicit NONE

! !INPUT PARAMETERS:
   real, intent(in) :: radius(:)       ! particle radius [m]
   real, dimension(:,:), intent(in) :: fraclake ! fraction of lake [1]
   real, dimension(:,:), intent(in) :: gwettop  ! surface soil wetness [1]
   real, dimension(:,:), intent(in) :: oro      ! land-ocean-ice mask [1]
   real, dimension(:,:), intent(in) :: u10m     ! 10-meter eastward wind [m/sec]
   real, dimension(:,:), intent(in) :: v10m     ! 10-meter northward wind [m/sec]
   real, dimension(:,:), intent(in) :: du_src   ! dust emissions [(sec^2 m^5)/kg]
   real, intent(in) :: Ch_DU   ! dust emission tuning coefficient [kg/(sec^2 m^5)]
   real, intent(in) :: grav    ! gravity [m/sec^2]

! !OUTPUT PARAMETERS:
!   real, pointer, intent(inout)  :: emissions(:,:)    ! Local emission [kg/(m^2 sec)]
   real, intent(inout)  :: emissions(:,:,:)    ! Local emission [kg/(m^2 sec)]

   integer, intent(out) :: rc  ! Error return code:


! !DESCRIPTION: Computes the dust emissions for one time step
!
! !REVISION HISTORY:
!
! 11Feb2020 E.Sherman - First attempt at refactor
!

! !Local Variables
   integer         ::  i, j, n
   real, parameter ::  air_dens = 1.25  ! Air density = 1.25 kg m-3
   real, parameter ::  soil_density  = 2650.  ! km m-3
   real            ::  diameter         ! dust effective diameter [m]
   real            ::  u_thresh0
   real            ::  u_thresh
   real            ::  w10m
   integer         ::  i1, i2, j1, j2, nbins
   integer         ::  dims(2)
!   real, allocatable ::  emissions_(:,:)

!EOP
!-------------------------------------------------------------------------
!  Begin

!  Initialize local variables
!  --------------------------
!   emissions(:,:,:) = 0.
!  Get dimensions
!  ---------------
   nbins = size(radius)
   dims = shape(u10m)
   i1 = 1; j1 = 1
   i2 = dims(1); j2 = dims(2)

!   allocate(emissions_(i2,j2))

!  Calculate the threshold velocity of wind erosion [m/s] for each radius
!  for a dry soil, as in Marticorena et al. [1997].
!  The parameterization includes the air density which is assumed
!  = 1.25 kg m-3 to speed the calculation.  The error in air density is
!  small compared to errors in other parameters.

   do n = 1, nbins
      diameter = 2. * radius(n)

      u_thresh0 = 0.13 * sqrt(soil_density*grav*diameter/air_dens) &
                       * sqrt(1.+6.e-7/(soil_density*grav*diameter**2.5)) &
              / sqrt(1.928*(1331.*(100.*diameter)**1.56+0.38)**0.092 - 1.)

!      emissions_(:,:) = 0.

!     Spatially dependent part of calculation
!     ---------------------------------------
      do j = j1, j2
         do i = i1, i2
            if ( oro(i,j) /= LAND ) cycle ! only over LAND gridpoints

            w10m = sqrt(u10m(i,j)**2.+v10m(i,j)**2.)
!           Modify the threshold depending on soil moisture as in Ginoux et al. [2001]
            if(gwettop(i,j) .lt. 0.5) then
               u_thresh = amax1(0.,u_thresh0* &
               (1.2+0.2*alog10(max(1.e-3,gwettop(i,j)))))

               if(w10m .gt. u_thresh) then
!                 Emission of dust [kg m-2 s-1]
                  emissions(i,j,n) = (1.-fraclake(i,j)) * w10m**2. * (w10m-u_thresh)
               endif
            endif !(gwettop(i,j) .lt. 0.5)
         end do ! i
      end do ! j
      emissions(:,:,n) = Ch_DU * du_src * emissions(:,:,n)
    end do ! n

   rc = __SUCCESS__

   end subroutine DustEmissionGOCART2G

!==================================================================================
!BOP
! !IROUTINE: DustEmissionK14

   subroutine DustEmissionK14( km, t_soil, w_top, rho_air,    &
                               z0, z, u_z, v_z, ustar,    &
                               f_land, f_snow,            &
                               f_src,                     &
                               f_sand, f_silt, f_clay,    &
                               texture, vegetation, gvf,  &
                               f_w, f_c, uts_gamma,       &
                               UNDEF, GRAV, VON_KARMAN,   &
                               opt_clay, Ch_DU,           &
                               emissions,                 &
                               u, u_t, u_ts,              &
                               R, H_w, f_erod,            &
                               rc )

! !USES:
   implicit none

! !INPUT PARAMETERS:
   integer, intent(in)              :: km         ! model levels
   real, dimension(:,:), intent(in) :: rho_air    ! air density
   real, dimension(:,:), intent(in) :: w_top      ! volumetric soil moisture in the top surface layer
   real, dimension(:,:), intent(in) :: t_soil     ! soil temperature
   real, dimension(:,:), intent(in) :: z0         ! aeolian aerodynamic roughness length
   real, dimension(:,:), intent(in) :: z, u_z, v_z! hight and wind at this height
   real, dimension(:,:), intent(in) :: ustar      ! friction velocity
   real, dimension(:,:), intent(in) :: f_land     ! land fraction
   real, dimension(:,:), intent(in) :: f_snow     ! snow fraction
   real, dimension(:,:), intent(in) :: f_src      ! dust source potential -- OBSOLETE
   real, dimension(:,:), intent(in) :: f_sand     ! sand fraction
   real, dimension(:,:), intent(in) :: f_silt     ! silt fraction
   real, dimension(:,:), intent(in) :: f_clay     ! clay fraction
   real, dimension(:,:), intent(in) :: texture    ! soil texture
   real, dimension(:,:), intent(in) :: vegetation ! vegetation categories (IGBP)
   real, dimension(:,:), intent(in) :: gvf        ! vegetation fraction

   integer, intent(in)              :: opt_clay   ! controls which clay&silt emissions term to use
   real, intent(in)                 :: Ch_DU      ! dust emission tuning coefficient [kg/(sec^2 m^5)]
   real,    intent(in)              :: f_w        ! factor to scale down soil moisture in the top 5cm to soil moisture in the top 1cm
   real,    intent(in)              :: f_c        ! scale down the wet sieving clay fraction to get it more in line with dry sieving measurements
   real,    intent(in)              :: uts_gamma  ! threshold friction velocity parameter 'gamma'
   real,    intent(in)              :: UNDEF      ! paramter for undefined varaible
   real,    intent(in)              :: GRAV       ! gravity
   real,    intent(in)              :: VON_KARMAN ! von karman constant

! !OUTPUT PARAMETERS:

   real, dimension(:,:,:), intent(out) :: emissions ! mass flux of emitted dust particles

   real, dimension(:,:), intent(out) :: u         ! aeolian friction velocity
   real, dimension(:,:), intent(out) :: u_t       ! threshold friction velocity
   real, dimension(:,:), intent(out) :: u_ts      ! threshold friction velocity over smooth surface

   real, dimension(:,:), intent(out) :: H_w       ! soil mosture correction
   real, dimension(:,:), intent(out) :: R         ! drag partition correction

   real, dimension(:,:), intent(out) :: f_erod    ! erodibility


   integer, intent(out) :: rc    ! Error return code:
                                 !  0 - all is well
                                 !  1 -

   character(len=*), parameter :: myname = 'DustEmissionK14'

!  !Local Variables

   real, dimension(:,:), allocatable :: w_g        ! gravimetric soil moisture
   real, dimension(:,:), allocatable :: w_gt       ! threshold gravimetric soil moisture

   real, dimension(:,:), allocatable :: f_veg      ! vegetation mask
   real, dimension(:,:), allocatable :: clay       ! 'corrected' clay fraction in '%'
   real, dimension(:,:), allocatable :: silt       ! 'corrected' silt fraction in '%'
   real, dimension(:,:), allocatable :: k_gamma    ! silt and clay term (gamma in K14 and I&K, 2017)
   real, dimension(:,:), allocatable :: z0s        ! smooth roughness length

   real, dimension(:,:), allocatable :: Dp_size    ! typical size of soil particles for optimal saltation
   real :: rho_p                              ! typical density of soil particles

   integer :: i, j, i1=1, i2, j1=1, j2, n

   real, parameter :: z0_valid = 0.08e-2      ! valid range of ARLEMS z0 is 0--0.08cm, z0 > 0.08cm is retreived but the data quality is low
   real, parameter :: z0_max = 6.25 * z0_valid! maximum roughness over arid surfaces
   real, parameter :: z0_    = 2.0e-4         ! representative aeolian aerodynamic roughness length z0 = 0.02cm

   real, parameter :: rho_water     = 1000.0  ! water density, 'kg m-3'
   real, parameter :: rho_soil_bulk = 1700.0  ! soil bulk density, 'kg m-3'
   real, parameter :: rho_soil      = 2500.0  ! soil particle density, 'kg m-3'

!  real, parameter :: f_w = 0.5               ! factor to scale down soil moisture in the top 5cm to soil moisture in the top 1cm
!  real, parameter :: f_c = 0.7               ! scale down the wet sieving clay fraction to get it more in line with dry sieving measurements

   ! Shao et al.
   real, parameter :: a_n = 0.0123
   real, parameter :: G   = 1.65e-4

   ! size of coarsest mode in the STATSGO/FAO soil type
   real, parameter :: Dc_soil(12) = (/ 710e-6, 710e-6, 125e-6, &
                                       125e-6, 125e-6, 160e-6, &
                                       710e-6, 125e-6, 125e-6, &
                                       160e-6, 125e-6,   2e-6 /)

! !DESCRIPTION: Computes the dust emissions for one time step
!
! !REVISION HISTORY:
!
!  15Aug2016, Darmenov - Initial implementation
!  15Dec2020, E.Sherman - Ported to GOCART2G process library

!EOP
!-------------------------------------------------------------------------
!  Begin...
   rc = 0
   i2 = ubound(t_soil,1)
   j2 = ubound(t_soil,2)

   allocate(w_g(i2,j2), w_gt(i2,j2), f_veg(i2,j2), clay(i2,j2), silt(i2,j2), k_gamma(i2,j2), source=0.0)
   allocate(z0s(i2,j2), Dp_size(i2,j2), source=0.0)

   ! typical size of soil particles for optimal saltation is about 75e-6m
   Dp_size = 75e-6

   ! typical density of soil particles, e.g. quartz grains
   rho_p = 2.65e3

   ! threshold friction velocity over smooth surface
   u_ts = UNDEF
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0))
       u_ts = sqrt(a_n * ( ((rho_p/rho_air) * GRAV * Dp_size) + uts_gamma / (rho_air * Dp_size)))
   end where

#if (0)
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  1) < 0.5) u_ts = u_ts * 1.176
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  2) < 0.5) u_ts = u_ts * 1.206
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  3) < 0.5) u_ts = u_ts * 1.234
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  4) < 0.5) u_ts = u_ts * 1.261
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  5) < 0.5) u_ts = u_ts * 1.272
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  6) < 0.5) u_ts = u_ts * 1.216
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  7) < 0.5) u_ts = u_ts * 1.211
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  8) < 0.5) u_ts = u_ts * 1.266
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture -  9) < 0.5) u_ts = u_ts * 1.222
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture - 10) < 0.5) u_ts = u_ts * 1.146
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture - 11) < 0.5) u_ts = u_ts * 1.271
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0) .and. abs(texture - 12) < 0.5) u_ts = u_ts * 1.216
#endif

   ! gravimetric soil moisture : scaled down to represent the values in the top 1cm and converted to '%'
   w_g = UNDEF
   where (f_land > 0.0)
#if (1)
       ! following Zender
       ! Q_s_ = 0.489 - 0.126*f_sand
       ! rho_soil_bulk = rho_soil*(1 - Q_s_)
       ! w_g = 100 * f_w * (rho_water / rho_soil_bulk) * w_top
       ! ...the equivalent one-liner
       w_g = 100 * f_w * rho_water / rho_soil / (1.0 - (0.489 - 0.126*f_sand)) * w_top
#else
       w_g = 100 * f_w * (rho_water / rho_soil_bulk) * w_top
#endif
   end where

   ! soil moisture correction following Fecan
   clay = UNDEF
   silt = UNDEF
   w_gt = UNDEF
   where ((f_land > 0.0) .and. (f_clay <= 1.0) .and. (f_clay >= 0.0))
       clay = f_c * f_clay
       silt = f_silt + (1.0-f_c)*f_clay   ! move the excess clay to the silt fraction

       w_gt = 14.0*clay*clay + 17.0*clay  ! w_gt in '%'
   end where

   H_w  = 1.0
#if (1)
   ! Fecan, 1999
   where ((f_land > 0.0) .and. (w_g > w_gt))
       H_w = sqrt(1.0 + 1.21*(w_g - w_gt)**0.68)
   end where
#else
   ! Shao, 1996
   where ((f_land > 0.0) .and. (w_top <= 1.0) .and. (w_top >= 0.0))
       H_w = exp(22.7*f_w *w_top)
   end where
#endif


   select case (opt_clay)
   case (1)
       ! following Ito and Kok, 2017
       k_gamma = 0.05

       where ((f_land > 0.0) .and. (clay < 0.2) .and. (clay >= 0.05))
           k_gamma = clay
       end where

       where ((f_land > 0.0) .and. (clay >= 0.2) .and. (clay <= 1.0))
           k_gamma = 0.2
       end where
   case (2)
       ! following Ito and Kok, 2017
       k_gamma = 1.0/1.4

       where ((f_land > 0.0) .and. (clay < 0.2) .and. (clay >= 0.0))
           k_gamma = 1.0 / (1.4 - clay - silt)
       end where

       where ((f_land > 0.0) .and. (clay >= 0.2) .and. (clay <= 1.0))
           k_gamma = 1.0 / (1.0 + clay - silt)
       end where
   case default
       ! following Kok et al, 2014
       k_gamma = 0.0

       where ((f_land > 0.0) .and. (clay <= 1.0) .and. (clay >= 0.0))
           k_gamma = clay
       end where
   end select


   ! roughness over smooth surface
   z0s = 125e-6
   do j = j1, j2
       do i = i1, i2
           if (texture(i,j) > 0 .and. texture(i,j) < 13) then
               z0s(i,j) = Dc_soil(nint(texture(i,j)))
           end if
       end do
   end do

   z0s = z0s / 30.0    ! z0s = MMD/x, where typically x is in the range 24--30; x=10 was recommended
                       ! as a more appropriate value for this parameter in a recent article

   ! drag partition correction
   R = 1.0
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > z0s))
#if (1)
       ! MacKinnon et al, 2004
       R = 1.0 - log(z0/z0s)/log(0.7 * (122.55/z0s)**0.8)
#else
       ! King et al, 2005, Darmenova et al, 2009, and K14-S1 use the corrected MB expression
       R = 1.0 - log(z0/z0s)/log(0.7 * (0.1/z0s)**0.8)
#endif
   end where


   ! *soil* friction velocity, see Equations 5, S.10, S11 in Kok et al, 2014 and the supplement paper
   u = UNDEF
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0))
#if (1)
       u = ustar
#else
       u = VON_KARMAN / log(z/z0) * sqrt(u_z*u_z + v_z*v_z)
#endif
       u = R * u           ! correction for roughness elements
   end where


   ! *soil* threshold friction velocity, Section 2.2 in Kok et al, 2014
   u_t = UNDEF
   where ((f_land > 0.0) .and. (z0 < z0_max) .and. (z0 > 0.0))
       u_t = u_ts * H_w    ! apply moisture correction
   end where


   ! erodibility
   f_erod = UNDEF
   where (f_land > 0.0)
       f_erod = 1.0
   end where

   ! erodibility parameterization - Laurent et al., 2008
   where ((f_land > 0.0) .and. (z0 > 3.0e-5) .and. (z0 < z0_max))
       f_erod = 0.7304 - 0.0804*log10(100*z0)
   end where

   ! bedrock
   where (abs(texture - 15) < 0.5) f_erod = 0.0


   ! vegetation mask
   f_veg = 0.0
   where ((f_land > 0.0) .and. abs(vegetation -  7) < 0.1) f_veg = 1.0  ! open shrublands
!  where ((f_land > 0.0) .and. abs(vegetation -  9) < 0.1) f_veg = 1.0  ! savannas
!  where ((f_land > 0.0) .and. abs(vegetation - 10) < 0.1) f_veg = 1.0  ! grasslands
!  where ((f_land > 0.0) .and. abs(vegetation - 12) < 0.1) f_veg = 1.0  ! croplands
   where ((f_land > 0.0) .and. abs(vegetation - 16) < 0.1) f_veg = 1.0  ! barren or sparsely vegetated

   ! vegetation mask: modulate with vegetation fraction
   where (f_land > 0.0 .and. gvf >= 0.0 .and. gvf < 0.8) f_veg = f_veg * (1 - gvf)


   ! final erodibility
   f_erod = f_erod * f_veg * f_land * (1.0 - f_snow)

   ! ...kludge to deal with high emissions in Australia
   where (f_src >= 0.0) f_erod = f_src * f_erod

   call VerticalDustFluxK14( i1, i2, j1, j2, km, &
                             u, u_t, rho_air,    &
                             f_erod, k_gamma,    &
                             emissions(:,:,1) )

   ! duplicate dust emissions across the 3rd dimension for use in call to UpdateAerosolState
   ! UpdateAerosolState expects surface dust emissions array of 3 dimensions(x, y, bin).
   emissions(:,:,1) = emissions(:,:,1) * Ch_DU
   do n = 2, size(emissions, dim=3)
      emissions(:,:,n) = emissions(:,:,1)
   end do

   end subroutine DustEmissionK14

!==================================================================================
!BOP
! !IROUTINE: VerticalDustFluxK14

   subroutine VerticalDustFluxK14( i1, i2, j1, j2, km, &
                                   u, u_t, rho_air, &
                                   f_erod, k_gamma,     &
                                   emissions )

! !USES:
   implicit none
! !INPUT PARAMETERS:
   integer, intent(in) ::  i1, i2, j1, j2, km

   real, dimension(:,:), intent(in) :: u           ! friction velocity, 'm s-1'
   real, dimension(:,:), intent(in) :: u_t         ! threshold friction velocity, 'm s-1'
   real, dimension(:,:), intent(in) :: rho_air     ! air density, 'kg m-3'
   real, dimension(:,:), intent(in) :: f_erod      ! erodibility
   real, dimension(:,:), intent(in) :: k_gamma     ! clay and silt dependent term that modulates the emissions

! !OUTPUT PARAMETERS:
   real, intent(out)    :: emissions(:,:)          ! total vertical dust mass flux, 'kg m-2 s-1'

   character(len=*), parameter :: myname = 'VerticalDustFluxK14'

! !Local Variables
   integer :: i, j
   real    :: u_st                     ! standardized threshold friction velocity
   real    :: C_d                      ! dust emission coefficient
   real    :: f_ust                    ! numerical term

  ! parameters from Kok et al. (2012, 2014)
   real, parameter :: rho_a0 = 1.225   ! standard atmospheric density at sea level, 'kg m-3'
   real, parameter :: u_st0  = 0.16    ! the minimal value of u* for an optimally erodible soil, 'm s-1'
   real, parameter :: C_d0   = 4.4e-5  ! C_d0 = (4.4 +/- 0.5)*1e-5
   real, parameter :: C_e    = 2.0     ! C_e  = 2.0 +/- 0.3
   real, parameter :: C_a    = 2.7     ! C_a  = 2.7 +/- 1.0

! !DESCRIPTION: Computes the dust emissions for one time step
!
! !REVISION HISTORY:
!
!  11Oct2011, Darmenov - For now use the GOCART emission scheme to
!                        calculate the total emission
!
!EOP
!-------------------------------------------------------------------------

  emissions = 0.0 ! total emission

  !  Vertical dust flux
  !  ------------------
  do j = j1, j2
      do i = i1, i2

          if ((f_erod(i,j) > 0.0) .and. (u(i,j) > u_t(i,j))) then
              u_st  = u_t(i,j) * sqrt(rho_air(i,j) / rho_a0)
              u_st  = max(u_st, u_st0)

              f_ust = (u_st - u_st0)/u_st0
              C_d = C_d0 * exp(-C_e * f_ust)

              emissions(i,j) = C_d * f_erod(i,j) * k_gamma(i,j) * rho_air(i,j)  &
                                   * ((u(i,j)*u(i,j) - u_t(i,j)*u_t(i,j)) / u_st) &
                                   * (u(i,j) / u_t(i,j))**(C_a * f_ust)
          end if

      end do
  end do

  ! all done
  end subroutine VerticalDustFluxK14