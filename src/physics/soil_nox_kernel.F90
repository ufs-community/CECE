module soil_nox_kernel_mod
    use iso_c_binding
    implicit none

contains

    pure function soil_temp_term(tc) result(t)
        real(c_double), intent(in) :: tc
        real(c_double) :: t
        if (tc <= 0.0d0) then
            t = 0.0d0
            return
        end if
        t = exp(0.103d0 * min(30.0d0, tc))
    end function

    pure function soil_wet_term(gw) result(w)
        real(c_double), intent(in) :: gw
        real(c_double) :: w
        w = 5.5d0 * gw * exp(-5.55d0 * gw * gw)
    end function

    subroutine run_soil_nox_fortran(temp_ptr, gwet_ptr, soil_nox_ptr, nx, ny, nz) bind(c, name="run_soil_nox_fortran")
        type(c_ptr), value :: temp_ptr, gwet_ptr, soil_nox_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: temp(:,:,:), gwet(:,:,:), soil_nox(:,:,:)
        real(c_double) :: tc, gw, t_term, w_term, emiss
        integer :: i, j, k

        call c_f_pointer(temp_ptr, temp, [int(nx), int(ny), int(nz)])
        call c_f_pointer(gwet_ptr, gwet, [int(nx), int(ny), int(nz)])
        call c_f_pointer(soil_nox_ptr, soil_nox, [int(nx), int(ny), int(nz)])

        do k = 1, nz
        do j = 1, ny
        do i = 1, nx
            tc = temp(i,j,k) - 273.15d0
            gw = gwet(i,j,k)
            t_term = soil_temp_term(tc)
            w_term = soil_wet_term(gw)

            ! UNITCONV = 1.0e-12 / 14.0 * 30.0
            emiss = 0.5d0 * (30.0d0 / 14.0d0 * 1.0d-12) * t_term * w_term
            if (k == 1) then
                soil_nox(i,j,k) = soil_nox(i,j,k) + emiss
            end if
        end do
        end do
        end do
    end subroutine

end module
