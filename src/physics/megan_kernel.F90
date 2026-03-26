module megan_kernel_mod
    use iso_c_binding
    implicit none

contains

    pure function get_gamma_lai(lai) result(g)
        real(c_double), intent(in) :: lai
        real(c_double) :: g
        g = 0.49d0 * lai / sqrt(1.0d0 + 0.2d0 * lai * lai)
    end function

    pure function get_gamma_t_li(temp, beta) result(g)
        real(c_double), intent(in) :: temp, beta
        real(c_double) :: g
        g = exp(beta * (temp - 303.0d0))
    end function

    pure function get_gamma_t_ld(T, PT_15, CT1, CEO) result(g)
        real(c_double), intent(in) :: T, PT_15, CT1, CEO
        real(c_double) :: g, e_opt, t_opt, x
        real(c_double), parameter :: R = 8.3144598d-3, CT2 = 200.0d0
        e_opt = CEO * exp(0.08d0 * (PT_15 - 297.0d0))
        t_opt = 313.0d0 + 0.6d0 * (PT_15 - 297.0d0)
        x = (1.0d0/t_opt - 1.0d0/T) / R
        g = e_opt * CT2 * exp(CT1 * x) / (CT2 - CT1 * (1.0d0 - exp(CT2 * x)))
        g = max(g, 0.0d0)
    end function

    pure function get_gamma_par(q_dir, q_diff, par_avg, suncos, doy) result(g)
        real(c_double), intent(in) :: q_dir, q_diff, par_avg, suncos
        integer, intent(in) :: doy
        real(c_double) :: g, pac_instant, pac_daily, ptoa, phi, aaa, bbb
        real(c_double), parameter :: PI = 3.14159265358979323846d0
        if (suncos <= 0.0d0) then
            g = 0.0d0
            return
        end if
        pac_instant = (q_dir + q_diff) * 4.766d0
        pac_daily = par_avg * 4.766d0
        ptoa = 3000.0d0 + 99.0d0 * cos(2.0d0 * PI * (dble(doy) - 10.0d0) / 365.0d0)
        phi = pac_instant / (suncos * ptoa)
        bbb = 1.0d0 + 0.0005d0 * (pac_daily - 400.0d0)
        aaa = (2.46d0 * bbb * phi) - (0.9d0 * phi * phi)
        g = max(suncos * aaa, 0.0d0)
    end function

    pure function get_gamma_co2(co2a) result(g)
        real(c_double), intent(in) :: co2a
        real(c_double) :: g
        g = 8.9406d0 / (1.0d0 + 8.9406d0 * 0.0024d0 * co2a)
    end function

    subroutine run_megan_fortran(temp_ptr, lai_ptr, pardr_ptr, pardf_ptr, suncos_ptr, isop_ptr, nx, ny, nz) &
        bind(c, name="run_megan_fortran")
        type(c_ptr), value :: temp_ptr, lai_ptr, pardr_ptr, pardf_ptr, suncos_ptr, isop_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: temp(:,:,:), lai(:,:,:), pardr(:,:,:), pardf(:,:,:), suncos(:,:,:), isop(:,:,:)
        real(c_double) :: t_val, l_val, sc_val, g_lai, g_t_li, g_t_ld, g_par, g_co2, megan_emis
        integer :: i, j, k

        call c_f_pointer(temp_ptr, temp, [int(nx), int(ny), int(nz)])
        call c_f_pointer(lai_ptr, lai, [int(nx), int(ny), int(nz)])
        call c_f_pointer(pardr_ptr, pardr, [int(nx), int(ny), int(nz)])
        call c_f_pointer(pardf_ptr, pardf, [int(nx), int(ny), int(nz)])
        call c_f_pointer(suncos_ptr, suncos, [int(nx), int(ny), int(nz)])
        call c_f_pointer(isop_ptr, isop, [int(nx), int(ny), int(nz)])

        do k = 1, nz
            if (k == 1) then ! Restricted to surface
                do j = 1, ny
                    do i = 1, nx
                        t_val = temp(i,j,k)
                        l_val = lai(i,j,k)
                        sc_val = suncos(i,j,k)

                        if (l_val > 0.0d0) then
                            g_lai = get_gamma_lai(l_val)
                            g_t_li = get_gamma_t_li(t_val, 0.13d0)
                            g_t_ld = get_gamma_t_ld(t_val, 297.0d0, 95.0d0, 2.0d0)
                            g_par = get_gamma_par(pardr(i,j,k), pardf(i,j,k), 400.0d0, sc_val, 180)
                            g_co2 = get_gamma_co2(400.0d0)

                            ! LDF = 1.0 for isoprene
                            megan_emis = (1.0d0 / 1.0101081d0) * 1.0d-9 * g_lai * g_par * g_t_ld * g_co2
                            isop(i,j,k) = isop(i,j,k) + megan_emis
                        end if
                    end do
                end do
            end if
        end do
    end subroutine run_megan_fortran


end module megan_kernel_mod
