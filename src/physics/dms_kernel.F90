module dms_kernel_mod
    use iso_c_binding
    implicit none

contains

    !> @brief Compute Schmidt number for DMS in water.
    !> @param tc Water temperature (deg C)
    !> @return sc Schmidt number
    pure function get_sc_w_dms(tc) result(sc)
        real(c_double), intent(in) :: tc
        real(c_double) :: sc
        sc = 2674.0d0 - 147.12d0 * tc + 3.726d0 * tc * tc - 0.038d0 * tc * tc * tc
    end function

    !> @brief Compute Nightingale transfer velocity for DMS.
    !> @param u10 Wind speed at 10m (m/s)
    !> @param sc_w Schmidt number
    !> @return kw Transfer velocity (cm/hr)
    pure function get_kw_nightingale(u10, sc_w) result(kw)
        real(c_double), intent(in) :: u10, sc_w
        real(c_double) :: kw
        kw = (0.222d0 * u10 * u10 + 0.333d0 * u10) * (sc_w / 600.0d0)**(-0.5d0)
    end function

    !> @brief Run the DMS emission kernel.
    !> @param u10m_ptr Pointer to 10m wind field
    !> @param tskin_ptr Pointer to skin temperature field
    !> @param seaconc_ptr Pointer to sea concentration field
    !> @param dms_emis_ptr Pointer to output DMS emission field
    !> @param nx, ny, nz Grid dimensions
    !> @details Called from C/C++ via bind(C).
    subroutine run_dms_fortran(u10m_ptr, tskin_ptr, seaconc_ptr, dms_emis_ptr, nx, ny, nz) bind(c, name="run_dms_fortran")
        type(c_ptr), value :: u10m_ptr, tskin_ptr, seaconc_ptr, dms_emis_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: u10m(:,:,:), tskin(:,:,:), seaconc(:,:,:), dms_emis(:,:,:)
        real(c_double) :: tk, tc, w, conc, sc_w, k_w
        integer :: i, j, k

        call c_f_pointer(u10m_ptr, u10m, [int(nx), int(ny), int(nz)])
        call c_f_pointer(tskin_ptr, tskin, [int(nx), int(ny), int(nz)])
        call c_f_pointer(seaconc_ptr, seaconc, [int(nx), int(ny), int(nz)])
        call c_f_pointer(dms_emis_ptr, dms_emis, [int(nx), int(ny), int(nz)])

        do k = 1, nz
        if (k == 1) then ! Restricted to surface
        do j = 1, ny
        do i = 1, nx
            tk = tskin(i,j,k)
            tc = tk - 273.15d0
            w = u10m(i,j,k)
            conc = seaconc(i,j,k)

            if (tc > -10.0d0) then
                sc_w = get_sc_w_dms(tc)
                k_w = get_kw_nightingale(w, sc_w)
                k_w = k_w / 360000.0d0 ! cm/hr -> m/s
                dms_emis(i,j,k) = dms_emis(i,j,k) + k_w * conc
            end if
        end do
        end do
        end if
        end do
    end subroutine

end module
