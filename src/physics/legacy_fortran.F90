module legacy_fortran_mod
    use iso_c_binding
    implicit none

contains

    subroutine run_legacy_fortran(temp_ptr, wind_ptr, nox_ptr, nx, ny, nz) bind(c, name="run_legacy_fortran")
        type(c_ptr), value :: temp_ptr, wind_ptr, nox_ptr
        integer(c_int), value :: nx, ny, nz

        real(c_double), pointer :: temp(:,:,:), wind(:,:,:), nox(:,:,:)

        call c_f_pointer(temp_ptr, temp, [int(nx), int(ny), int(nz)])
        call c_f_pointer(wind_ptr, wind, [int(nx), int(ny), int(nz)])
        call c_f_pointer(nox_ptr, nox, [int(nx), int(ny), int(nz)])

        ! Simple operation: add 1.0 to all NOX emissions
        nox = nox + 1.0
    end subroutine

end module
