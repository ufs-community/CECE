module lightning_kernel_mod
    use iso_c_binding
    implicit none

contains

    !> @brief Compute lightning NOx yield for a given rate and molecular weight.
    !> @param rate Lightning flash rate (flashes/s)
    !> @param mw_no Molecular weight of NO (g/mol)
    !> @param is_land True if land, False if ocean
    !> @return yield NOx yield (mol/s)
    pure function get_lightning_yield(rate, mw_no, is_land) result(yield)
        real(c_double), intent(in) :: rate, mw_no
        logical, intent(in) :: is_land
        real(c_double) :: yield, yield_molec
        if (is_land) then
            yield_molec = 3.011d26
        else
            yield_molec = 1.566d26
        end if
        yield = (rate * yield_molec) * (mw_no / 1000.0d0) / (6.022d23 * 1.0d6)
    end function

    !> @brief Run the lightning kernel for NOx emissions.
    !> @param conv_depth_ptr Pointer to convective depth field
    !> @param light_nox_ptr Pointer to output NOx field
    !> @param nx, ny, nz Grid dimensions
    !> @details Called from C/C++ via bind(C).
    subroutine run_lightning_fortran(conv_depth_ptr, light_nox_ptr, nx, ny, nz) bind(c, name="run_lightning_fortran")
        type(c_ptr), value :: conv_depth_ptr, light_nox_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: conv_depth(:,:,:), light_nox(:,:,:)
        real(c_double) :: h, h_km, flash_rate, total_yield
        integer :: i, j, k

        call c_f_pointer(conv_depth_ptr, conv_depth, [int(nx), int(ny), int(nz)])
        call c_f_pointer(light_nox_ptr, light_nox, [int(nx), int(ny), int(nz)])

        do k = 1, nz
        do j = 1, ny
        do i = 1, nx
            h = conv_depth(i,j,k)
            if (h > 0.0d0) then
                h_km = h / 1000.0d0
                flash_rate = 3.44d-5 * h_km**4.9d0
                ! Default to is_land=.true. for now as land mask isn't passed yet
                total_yield = get_lightning_yield(flash_rate, 30.0d0, .true.)
                light_nox(i,j,k) = light_nox(i,j,k) + total_yield / dble(nz)
            end if
        end do
        end do
        end do
    end subroutine

end module
