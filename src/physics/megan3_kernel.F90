!> @file megan3_kernel.F90
!> @brief Fortran kernel for the full MEGAN3 biogenic emission scheme.
!>
!> Implements the run_megan3_fortran subroutine with 19-class emission
!> computation, all gamma factors, LDF partitioning, and speciation
!> conversion to mechanism species. Produces numerically identical results
!> to the C++ Megan3Scheme within 1e-6 relative tolerance.
!>
!> @author CECE Team
!> @date 2024

module megan3_kernel_mod
    use iso_c_binding
    implicit none

    ! ========================================================================
    ! Constants matching the C++ implementation in cece_megan.cpp / cece_megan3.cpp
    ! ========================================================================
    integer, parameter :: NUM_CLASSES = 19
    real(c_double), parameter :: NORM_FAC = 1.0d0 / 1.0101081d0
    real(c_double), parameter :: LAI_C1 = 0.49d0
    real(c_double), parameter :: LAI_C2 = 0.2d0
    real(c_double), parameter :: STD_TEMP = 303.0d0
    real(c_double), parameter :: GAS_CONSTANT = 8.3144598d-3
    real(c_double), parameter :: CT2_CONST = 200.0d0
    real(c_double), parameter :: T_OPT_C1 = 313.0d0
    real(c_double), parameter :: T_OPT_C2 = 0.6d0
    real(c_double), parameter :: E_OPT_COEFF = 0.08d0
    real(c_double), parameter :: WM2_TO_UMOL = 4.766d0
    real(c_double), parameter :: PTOA_C1 = 3000.0d0
    real(c_double), parameter :: PTOA_C2 = 99.0d0
    real(c_double), parameter :: GP_C1 = 1.0d0
    real(c_double), parameter :: GP_C2 = 0.0005d0
    real(c_double), parameter :: GP_C3 = 2.46d0
    real(c_double), parameter :: GP_C4 = 0.9d0
    real(c_double), parameter :: PI = 3.14159265358979323846d0

    ! NO class index (0-based in C++, but we use 1-based in Fortran loops
    ! and compare with 0-based class_indices from C++)
    integer, parameter :: NO_CLASS_IDX = 7  ! 0-based index for NO class

    ! Default per-class coefficients matching cece_emission_activity.cpp
    real(c_double), parameter :: DEFAULT_LDF(NUM_CLASSES) = (/ &
        0.9996d0, 0.80d0, 0.10d0, 0.10d0, 0.10d0, 0.10d0, 0.10d0, &
        0.00d0,   0.50d0, 0.50d0, 0.80d0, 0.20d0, 0.80d0, 0.00d0, &
        0.00d0,   0.20d0, 1.00d0, 0.20d0, 0.00d0 /)

    real(c_double), parameter :: DEFAULT_CT1(NUM_CLASSES) = (/ &
        95.0d0, 80.0d0, 80.0d0, 80.0d0, 80.0d0, 80.0d0, 80.0d0, &
        80.0d0, 80.0d0, 80.0d0, 60.0d0, 80.0d0, 60.0d0, 80.0d0, &
        80.0d0, 80.0d0, 95.0d0, 80.0d0, 80.0d0 /)

    real(c_double), parameter :: DEFAULT_CLEO(NUM_CLASSES) = (/ &
        2.0d0, 1.83d0, 1.83d0, 1.83d0, 1.83d0, 1.83d0, 1.83d0, &
        1.83d0, 1.83d0, 1.83d0, 1.83d0, 1.83d0, 1.83d0, 1.83d0, &
        1.83d0, 1.83d0, 2.0d0,  1.83d0, 1.83d0 /)

    real(c_double), parameter :: DEFAULT_BETA(NUM_CLASSES) = (/ &
        0.13d0, 0.13d0, 0.10d0, 0.10d0, 0.10d0, 0.10d0, 0.10d0, &
        0.10d0, 0.17d0, 0.17d0, 0.08d0, 0.10d0, 0.08d0, 0.10d0, &
        0.13d0, 0.10d0, 0.13d0, 0.10d0, 0.10d0 /)

    real(c_double), parameter :: DEFAULT_ANEW(NUM_CLASSES) = (/ &
        0.05d0, 0.05d0, 2.0d0, 2.0d0, 2.0d0, 2.0d0, 2.0d0, &
        0.00d0, 0.05d0, 0.05d0, 3.5d0, 0.05d0, 3.5d0, 0.05d0, &
        0.05d0, 0.05d0, 0.05d0, 0.05d0, 0.05d0 /)

    real(c_double), parameter :: DEFAULT_AGRO(NUM_CLASSES) = (/ &
        0.6d0, 0.6d0, 1.8d0, 1.8d0, 1.8d0, 1.8d0, 1.8d0, &
        0.0d0, 0.6d0, 0.6d0, 3.0d0, 0.6d0, 3.0d0, 0.6d0, &
        0.6d0, 0.6d0, 0.6d0, 0.6d0, 0.6d0 /)

    real(c_double), parameter :: DEFAULT_AMAT(NUM_CLASSES) = (/ &
        1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, &
        1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, &
        1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0 /)

    real(c_double), parameter :: DEFAULT_AOLD(NUM_CLASSES) = (/ &
        0.9d0, 0.9d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, 1.0d0, &
        0.0d0, 0.9d0, 0.9d0, 1.2d0, 0.9d0, 1.2d0, 0.9d0, &
        0.9d0, 0.9d0, 0.9d0, 0.9d0, 0.9d0 /)

    ! Default AEF values matching cece_megan3.cpp kDefaultAef
    real(c_double), parameter :: DEFAULT_AEF(NUM_CLASSES) = (/ &
        1.0d-9,  2.0d-10, 3.0d-10, 3.0d-10, 3.0d-10, 3.0d-10, 3.0d-10, &
        0.0d0,   1.0d-10, 1.0d-10, 5.0d-10, 2.0d-10, 2.0d-10, 1.0d-10, &
        1.0d-10, 1.0d-10, 1.0d-10, 1.0d-10, 3.0d-10 /)

    ! Pre-computed gamma_co2 matching C++ default (Possell, 400 ppm)
    ! gamma_co2 = 8.9406 / (1.0 + 8.9406 * 0.0024 * 400.0) = 8.9406 / 9.5225376
    real(c_double), parameter :: GAMMA_CO2_DEFAULT = 8.9406d0 / &
        (1.0d0 + 8.9406d0 * 0.0024d0 * 400.0d0)

contains

    ! ========================================================================
    ! Gamma factor functions matching cece_megan.cpp exactly
    ! ========================================================================

    !> @brief LAI gamma factor matching get_gamma_lai in cece_megan.cpp
    pure function gamma_lai(lai_val, c1, c2, is_bidir) result(g)
        real(c_double), intent(in) :: lai_val, c1, c2
        logical, intent(in) :: is_bidir
        real(c_double) :: g

        if (is_bidir) then
            if (lai_val <= 6.0d0) then
                if (lai_val <= 2.0d0) then
                    g = 0.5d0 * lai_val
                else
                    g = 1.0d0 - 0.0625d0 * (lai_val - 2.0d0)
                end if
            else
                g = 0.75d0
            end if
        else
            g = c1 * lai_val / sqrt(1.0d0 + c2 * lai_val * lai_val)
        end if
    end function gamma_lai

    !> @brief Leaf age gamma factor matching get_gamma_age in cece_megan.cpp
    pure function gamma_age(cmlai, pmlai, dbtwn, tt, an, ag, am, ao) result(g)
        real(c_double), intent(in) :: cmlai, pmlai, dbtwn, tt
        real(c_double), intent(in) :: an, ag, am, ao
        real(c_double) :: g
        real(c_double) :: fnew, fgro, fmat, fold, ti, tm

        fnew = 0.0d0
        fgro = 0.0d0
        fmat = 0.0d0
        fold = 0.0d0

        if (tt <= 303.0d0) then
            ti = 5.0d0 + 0.7d0 * (300.0d0 - tt)
        else
            ti = 2.9d0
        end if
        tm = 2.3d0 * ti

        if (cmlai == pmlai) then
            fnew = 0.0d0
            fgro = 0.1d0
            fmat = 0.8d0
            fold = 0.1d0
        else if (cmlai > pmlai) then
            if (dbtwn > ti) then
                fnew = (ti / dbtwn) * (1.0d0 - pmlai / cmlai)
            else
                fnew = 1.0d0 - (pmlai / cmlai)
            end if

            if (dbtwn > tm) then
                fmat = (pmlai / cmlai) + ((dbtwn - tm) / dbtwn) * &
                       (1.0d0 - pmlai / cmlai)
            else
                fmat = pmlai / cmlai
            end if

            fgro = 1.0d0 - fnew - fmat
            fold = 0.0d0
        else
            fnew = 0.0d0
            fgro = 0.0d0
            fold = (pmlai - cmlai) / pmlai
            fmat = 1.0d0 - fold
        end if

        g = fnew * an + fgro * ag + fmat * am + fold * ao
        g = max(g, 0.0d0)
    end function gamma_age

    !> @brief Soil moisture gamma factor matching get_gamma_sm in cece_megan.cpp
    pure function gamma_sm(gwetroot, is_ald2_or_eoh) result(g)
        real(c_double), intent(in) :: gwetroot
        logical, intent(in) :: is_ald2_or_eoh
        real(c_double) :: g, gwet_clamped

        g = 1.0d0
        gwet_clamped = min(max(gwetroot, 0.0d0), 1.0d0)

        if (is_ald2_or_eoh) then
            g = max(20.0d0 * gwet_clamped - 17.0d0, 1.0d0)
        end if
    end function gamma_sm

    !> @brief Temperature gamma (light-independent) matching get_gamma_t_li
    pure function gamma_t_li(temp_val, beta_val, t_standard) result(g)
        real(c_double), intent(in) :: temp_val, beta_val, t_standard
        real(c_double) :: g

        g = exp(beta_val * (temp_val - t_standard))
    end function gamma_t_li

    !> @brief Temperature gamma (light-dependent) matching get_gamma_t_ld
    pure function gamma_t_ld(T, PT_15, CT1, CEO) result(g)
        real(c_double), intent(in) :: T, PT_15, CT1, CEO
        real(c_double) :: g, e_opt, t_opt, x

        e_opt = CEO * exp(E_OPT_COEFF * (PT_15 - 297.0d0))
        t_opt = T_OPT_C1 + T_OPT_C2 * (PT_15 - 297.0d0)
        x = (1.0d0 / t_opt - 1.0d0 / T) / GAS_CONSTANT
        g = e_opt * CT2_CONST * exp(CT1 * x) / &
            (CT2_CONST - CT1 * (1.0d0 - exp(CT2_CONST * x)))
        g = max(g, 0.0d0)
    end function gamma_t_ld

    !> @brief PAR gamma factor (PCEEA) matching get_gamma_par_pceea
    pure function gamma_par_pceea(q_dir, q_diff, par_avg, suncos_val, doy) result(g)
        real(c_double), intent(in) :: q_dir, q_diff, par_avg, suncos_val
        integer, intent(in) :: doy
        real(c_double) :: g, pac_instant, pac_daily, ptoa, phi, bbb, aaa

        if (suncos_val <= 0.0d0) then
            g = 0.0d0
            return
        end if

        pac_instant = (q_dir + q_diff) * WM2_TO_UMOL
        pac_daily = par_avg * WM2_TO_UMOL
        ptoa = PTOA_C1 + PTOA_C2 * cos(2.0d0 * PI * (dble(doy) - 10.0d0) / 365.0d0)
        phi = pac_instant / (suncos_val * ptoa)
        bbb = GP_C1 + GP_C2 * (pac_daily - 400.0d0)
        aaa = (GP_C3 * bbb * phi) - (GP_C4 * phi * phi)
        g = suncos_val * aaa
        g = max(g, 0.0d0)
    end function gamma_par_pceea

    ! ========================================================================
    ! Main MEGAN3 Fortran kernel
    ! ========================================================================

    !> @brief MEGAN3 Fortran kernel for biogenic emission calculations.
    !>
    !> Computes 19-class emissions with gamma factors and LDF partitioning,
    !> then performs speciation conversion to mechanism species.
    !> Produces numerically identical results to the C++ Megan3Scheme.
    !>
    !> The output buffer is laid out as: output(cell, species) where
    !> cell = (i-1) + (j-1)*nx + (k-1)*nx*ny (0-based from C++ perspective)
    !> and species is 0-based mechanism species index.
    !> Total size: nx * ny * nz * num_output_species
    subroutine run_megan3_fortran(temp, lai, lai_prev, pardr, pardf, suncos, &
                                  soil_moisture, wind_speed, soil_nox, &
                                  output, nx, ny, nz, num_output_species, &
                                  conversion_factors, class_indices, &
                                  mechanism_indices, molecular_weights, &
                                  num_mappings, num_mechanism_species) &
        bind(C, name="run_megan3_fortran")

        integer(c_int), value, intent(in) :: nx, ny, nz
        integer(c_int), value, intent(in) :: num_output_species
        integer(c_int), value, intent(in) :: num_mappings
        integer(c_int), value, intent(in) :: num_mechanism_species

        real(c_double), intent(in) :: temp(nx, ny, nz)
        real(c_double), intent(in) :: lai(nx, ny, nz)
        real(c_double), intent(in) :: lai_prev(nx, ny, nz)
        real(c_double), intent(in) :: pardr(nx, ny, nz)
        real(c_double), intent(in) :: pardf(nx, ny, nz)
        real(c_double), intent(in) :: suncos(nx, ny, nz)
        real(c_double), intent(in) :: soil_moisture(nx, ny, nz)
        real(c_double), intent(in) :: wind_speed(nx, ny, nz)
        real(c_double), intent(in) :: soil_nox(nx, ny, nz)
        real(c_double), intent(inout) :: output(nx * ny * nz * num_output_species)
        real(c_double), intent(in) :: conversion_factors(num_mappings)
        integer(c_int), intent(in) :: class_indices(num_mappings)
        integer(c_int), intent(in) :: mechanism_indices(num_mappings)
        real(c_double), intent(in) :: molecular_weights(num_mechanism_species)

        ! Local variables
        integer :: i, j, k, c, m
        integer :: cell_idx, out_idx, cls_idx, mech_idx
        real(c_double) :: T_val, L_val, L_prev_val, sc_val
        real(c_double) :: pdr_val, pdf_val, gwet_val, ws_val
        real(c_double) :: T_AVG_15, PAR_AVG, dbtwn
        integer :: doy

        ! Gamma factors
        real(c_double) :: g_lai_c, g_age_c, g_sm_val, g_t_li_val, g_t_ld_val
        real(c_double) :: g_par_val, ldf_combined, emission

        ! Per-class coefficients (use defaults)
        real(c_double) :: ldf_val, ct1_val, cleo_val, beta_val
        real(c_double) :: anew_val, agro_val, amat_val, aold_val
        real(c_double) :: aef_val

        ! Class totals for speciation
        real(c_double) :: class_totals(NUM_CLASSES)

        integer :: num_cells, total_out

        num_cells = nx * ny * nz

        ! Zero the output buffer
        total_out = num_cells * num_output_species
        do m = 1, total_out
            output(m) = 0.0d0
        end do

        ! Averaged values (defaults matching C++ code)
        T_AVG_15 = 297.0d0
        PAR_AVG = 400.0d0
        doy = 180
        dbtwn = 30.0d0

        ! Loop over grid cells (Fortran column-major: i fastest)
        do k = 1, nz
            do j = 1, ny
                do i = 1, nx
                    T_val = temp(i, j, k)
                    L_val = lai(i, j, k)
                    sc_val = suncos(i, j, k)
                    pdr_val = pardr(i, j, k)
                    pdf_val = pardf(i, j, k)
                    L_prev_val = lai_prev(i, j, k)
                    gwet_val = soil_moisture(i, j, k)
                    ws_val = wind_speed(i, j, k)

                    ! Skip cells with zero LAI (matching C++ early return)
                    if (L_val <= 0.0d0) cycle

                    ! Compute shared PAR gamma factor
                    g_par_val = gamma_par_pceea(pdr_val, pdf_val, PAR_AVG, &
                                                sc_val, doy)

                    ! Compute 19 class totals
                    do c = 1, NUM_CLASSES
                        ldf_val = DEFAULT_LDF(c)
                        ct1_val = DEFAULT_CT1(c)
                        cleo_val = DEFAULT_CLEO(c)
                        beta_val = DEFAULT_BETA(c)
                        anew_val = DEFAULT_ANEW(c)
                        agro_val = DEFAULT_AGRO(c)
                        amat_val = DEFAULT_AMAT(c)
                        aold_val = DEFAULT_AOLD(c)
                        aef_val = DEFAULT_AEF(c)

                        ! Per-class gamma factors
                        ! Note: bidirectional is false for all classes by default
                        g_lai_c = gamma_lai(L_val, LAI_C1, LAI_C2, .false.)
                        g_age_c = gamma_age(L_val, L_prev_val, dbtwn, T_val, &
                                            anew_val, agro_val, amat_val, aold_val)
                        g_sm_val = gamma_sm(gwet_val, .false.)
                        g_t_li_val = gamma_t_li(T_val, beta_val, STD_TEMP)
                        g_t_ld_val = gamma_t_ld(T_val, T_AVG_15, ct1_val, cleo_val)

                        ! LDF partitioning:
                        ! (1-LDF)*gamma_t_li + LDF*gamma_par*gamma_t_ld
                        ldf_combined = (1.0d0 - ldf_val) * g_t_li_val + &
                                       ldf_val * g_par_val * g_t_ld_val

                        ! Combined emission for this class
                        emission = NORM_FAC * aef_val * g_lai_c * g_age_c * &
                                   g_sm_val * GAMMA_CO2_DEFAULT * ldf_combined

                        ! Special handling for NO class (index 8 in Fortran,
                        ! 0-based index 7 in C++)
                        if (c == NO_CLASS_IDX + 1) then
                            emission = soil_nox(i, j, k)
                        end if

                        class_totals(c) = emission
                    end do

                    ! --------------------------------------------------------
                    ! Speciation: disaggregate class totals to mechanism species
                    ! --------------------------------------------------------
                    ! cell_idx is 0-based linear index matching C++ layout:
                    ! cell = i + j * nx (for nz=1, k=1)
                    ! For general case: cell = (i-1) + (j-1)*nx + (k-1)*nx*ny
                    cell_idx = (i - 1) + (j - 1) * nx + (k - 1) * nx * ny

                    do m = 1, num_mappings
                        ! class_indices are 0-based from C++
                        cls_idx = class_indices(m) + 1  ! Convert to 1-based
                        ! mechanism_indices are 0-based from C++
                        mech_idx = mechanism_indices(m)  ! Keep 0-based for output

                        ! Output index: species-major layout
                        ! output[(species * num_cells) + cell]
                        ! In Fortran 1-based: output(mech_idx * num_cells + cell_idx + 1)
                        out_idx = mech_idx * num_cells + cell_idx + 1

                        ! Accumulate: class_total * scale_factor * molecular_weight
                        output(out_idx) = output(out_idx) + &
                            class_totals(cls_idx) * &
                            conversion_factors(m) * &
                            molecular_weights(mech_idx + 1)
                    end do

                end do
            end do
        end do

    end subroutine run_megan3_fortran

end module megan3_kernel_mod
