module legacy_fortran_mod
    use iso_c_binding
    use ESMF
    implicit none

contains

    subroutine aces_get_clock_time(clock, ymd, tod) bind(c, name="aces_get_clock_time")
        type(c_ptr), value :: clock
        integer(c_int), intent(out) :: ymd, tod

        type(ESMF_Clock), pointer :: clk_ptr
        type(ESMF_Time) :: currTime
        integer :: yy, mm, dd, ss

        call c_f_pointer(clock, clk_ptr)
        call ESMF_ClockGet(clk_ptr, currTime=currTime)
        call ESMF_TimeGet(currTime, yy=yy, mm=mm, dd=dd, s=ss)

        ymd = yy*10000 + mm*100 + dd
        tod = ss
    end subroutine

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
