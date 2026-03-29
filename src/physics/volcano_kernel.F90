module volcano_kernel_mod
    use iso_c_binding
    implicit none

contains

    subroutine run_volcano_fortran(zsfc_ptr, bxheight_ptr, so2_ptr, nx, ny, nz) bind(c, name="run_volcano_fortran")
        type(c_ptr), value :: zsfc_ptr, bxheight_ptr, so2_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: zsfc(:,:,:), bxheight(:,:,:), so2(:,:,:)
        real(c_double) :: z_bot_box, z_top_box, z_bot_volc, z_top_volc, plume_hgt, overlap, frac
        real(c_double), parameter :: volc_sulf = 1.0d0, volc_elv = 600.0d0, volc_cld = 2000.0d0
        integer :: i, j, k, l

        call c_f_pointer(zsfc_ptr, zsfc, [int(nx), int(ny), int(nz)])
        call c_f_pointer(bxheight_ptr, bxheight, [int(nx), int(ny), int(nz)])
        call c_f_pointer(so2_ptr, so2, [int(nx), int(ny), int(nz)])

        ! Logic for mock volcano at (2,2) (Fortran indexing)
        i = 2
        j = 2
        do k = 1, nz
            z_bot_box = zsfc(i,j,1)
            do l = 1, k-1
                z_bot_box = z_bot_box + bxheight(i,j,l)
            end do
            z_top_box = z_bot_box + bxheight(i,j,k)

            z_bot_volc = max(volc_elv, zsfc(i,j,1))
            z_top_volc = max(volc_cld, zsfc(i,j,1))

            if (z_bot_volc /= z_top_volc) then
                z_bot_volc = z_top_volc - (z_top_volc - z_bot_volc) / 3.0d0
            end if

            plume_hgt = z_top_volc - z_bot_volc
            if (plume_hgt <= 0.0d0) then
                if (k == 1) so2(i,j,k) = so2(i,j,k) + volc_sulf
                cycle
            end if

            if (z_bot_volc >= z_top_box .or. z_top_volc <= z_bot_box) cycle

            overlap = min(z_top_volc, z_top_box) - max(z_bot_volc, z_bot_box)
            frac = overlap / plume_hgt
            so2(i,j,k) = so2(i,j,k) + frac * volc_sulf
        end do
    end subroutine

end module
