module sea_salt_kernel_mod
    use iso_c_binding
    implicit none

contains

    pure function gong_source(u10, r80) result(df_dr80)
        real(c_double), intent(in) :: u10, r80
        real(c_double) :: df_dr80
        real(c_double) :: a, b
        if (r80 <= 0.0d0) then
            df_dr80 = 0.0d0
            return
        end if
        a = 4.7d0 * (1.0d0 + 30.0d0 * r80)**(-0.017d0 * r80**(-1.44d0))
        b = (0.433d0 - log10(r80)) / 0.433d0
        df_dr80 = 1.373d0 * u10**3.41d0 * r80**(-a) * &
                  (1.0d0 + 0.057d0 * r80**3.45d0) * 10.0d0**(1.607d0 * exp(-b * b))
    end function

    subroutine run_sea_salt_fortran(u10m_ptr, tskin_ptr, sala_ptr, salc_ptr, nx, ny, nz) bind(c, name="run_sea_salt_fortran")
        type(c_ptr), value :: u10m_ptr, tskin_ptr, sala_ptr, salc_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: u10m(:,:,:), tskin(:,:,:), sala(:,:,:), salc(:,:,:)
        real(c_double) :: sst, scale, u, r, r_mid, r80_mid, df_dr80, n_particles, total_kg
        real(c_double), parameter :: pi = 3.14159265358979323846d0
        real(c_double), parameter :: dr = 0.05d0
        real(c_double), parameter :: betha = 2.0d0
        real(c_double), parameter :: ss_dens = 2200.0d0
        integer :: i, j, k

        call c_f_pointer(u10m_ptr, u10m, [int(nx), int(ny), int(nz)])
        call c_f_pointer(tskin_ptr, tskin, [int(nx), int(ny), int(nz)])
        call c_f_pointer(sala_ptr, sala, [int(nx), int(ny), int(nz)])
        call c_f_pointer(salc_ptr, salc, [int(nx), int(ny), int(nz)])

        do k = 1, nz
        if (k == 1) then ! Restricted to surface
        do j = 1, ny
        do i = 1, nx
            u = u10m(i,j,k)
            sst = tskin(i,j,k) - 273.15d0
            sst = max(0.0d0, min(30.0d0, sst))
            scale = 0.329d0 + 0.0904d0*sst - 0.00717d0*sst**2 + 0.000207d0*sst**3

            ! Integrate SALA
            total_kg = 0.0d0
            r = 0.01d0
            do while (r < 0.5d0)
                r_mid = r + 0.5d0 * dr
                r80_mid = r_mid * betha
                df_dr80 = gong_source(u, r80_mid)
                n_particles = df_dr80 * betha * dr
                total_kg = total_kg + n_particles * (4.0d0/3.0d0 * pi * (r_mid * 1.0d-6)**3 * ss_dens)
                r = r + dr
            end do
            sala(i,j,k) = sala(i,j,k) + scale * total_kg

            ! Integrate SALC
            total_kg = 0.0d0
            r = 0.5d0
            do while (r < 8.0d0)
                r_mid = r + 0.5d0 * dr
                r80_mid = r_mid * betha
                df_dr80 = gong_source(u, r80_mid)
                n_particles = df_dr80 * betha * dr
                total_kg = total_kg + n_particles * (4.0d0/3.0d0 * pi * (r_mid * 1.0d-6)**3 * ss_dens)
                r = r + dr
            end do
            salc(i,j,k) = salc(i,j,k) + scale * total_kg
        end do
        end do
        end if
        end do
    end subroutine

end module
