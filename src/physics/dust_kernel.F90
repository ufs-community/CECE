module dust_kernel_mod
    use iso_c_binding
    implicit none

contains

    pure function get_u_ts0(den, diam, g, rhoa) result(u_ts0)
        real(c_double), intent(in) :: den, diam, g, rhoa
        real(c_double) :: u_ts0, reynol, alpha, beta, gamma
        reynol = 1331.0d0 * diam**(1.56d0) + 0.38d0
        alpha = den * g * diam / rhoa
        beta = 1.0d0 + (6.0d-3 / (den * g * diam**(2.5d0)))
        gamma = (1.928d0 * reynol**(0.092d0)) - 1.0d0
        u_ts0 = 129.0d-5 * sqrt(alpha) * sqrt(beta) / sqrt(gamma)
    end function

    subroutine run_dust_fortran(u10m_ptr, gwet_ptr, sand_ptr, dust_emis_ptr, nx, ny, nz) bind(c, name="run_dust_fortran")
        type(c_ptr), value :: u10m_ptr, gwet_ptr, sand_ptr, dust_emis_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: u10m(:,:,:), gwet(:,:,:), sand(:,:,:), dust_emis(:,:,:)
        real(c_double) :: u10, w2, u_ts0, u_ts, srce, flux
        real(c_double), parameter :: den = 2.5d0, diam = 1.46d-4, G = 980.665d0, RHOA = 1.25d-3, CH_DUST = 9.375d-10
        integer :: i, j, k

        call c_f_pointer(u10m_ptr, u10m, [int(nx), int(ny), int(nz)])
        call c_f_pointer(gwet_ptr, gwet, [int(nx), int(ny), int(nz)])
        call c_f_pointer(sand_ptr, sand, [int(nx), int(ny), int(nz)])
        call c_f_pointer(dust_emis_ptr, dust_emis, [int(nx), int(ny), int(nz)])

        do k = 1, nz
        if (k == 1) then ! Restricted to surface
        do j = 1, ny
        do i = 1, nx
            u10 = u10m(i,j,k)
            w2 = u10 * u10
            u_ts0 = get_u_ts0(den, diam, G, RHOA)

            if (gwet(i,j,k) < 0.2d0) then
                u_ts = u_ts0 * (1.2d0 + 0.2d0 * log10(max(1.0d-3, gwet(i,j,k))))
            else
                u_ts = 100.0d0
            end if

            if (sqrt(w2) > u_ts) then
                srce = sand(i,j,k)
                flux = CH_DUST * srce * w2 * (sqrt(w2) - u_ts)
                dust_emis(i,j,k) = dust_emis(i,j,k) + max(0.0d0, flux)
            end if
        end do
        end do
        end if
        end do
    end subroutine

end module
