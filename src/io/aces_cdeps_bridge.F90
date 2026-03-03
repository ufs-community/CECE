module aces_cdeps_bridge_mod
    use iso_c_binding
    use ESMF
    use cdeps_inline_mod

    implicit none
    private

    public :: aces_cdeps_init
    public :: aces_cdeps_advance
    public :: aces_cdeps_get_ptr

contains

    subroutine aces_cdeps_init(gcomp_c, clock_c, mesh_c, stream_file_c, rc) bind(c, name="aces_cdeps_init")
        type(c_ptr), value :: gcomp_c, clock_c, mesh_c, stream_file_c
        integer(c_int), intent(out) :: rc

        type(ESMF_GridComp) :: gcomp
        type(ESMF_Clock) :: clock
        type(ESMF_Mesh) :: mesh
        character(kind=c_char), pointer :: stream_file_ptr(:)
        character(len=ESMF_MAXSTR) :: stream_file
        integer :: i, f_rc

        gcomp = ESMF_GridCompFromC(gcomp_c)
        clock = ESMF_ClockFromC(clock_c)
        mesh = ESMF_MeshFromC(mesh_c)

        call c_f_pointer(stream_file_c, stream_file_ptr, [ESMF_MAXSTR])
        stream_file = ""
        do i = 1, ESMF_MAXSTR
            if (stream_file_ptr(i) == c_null_char) exit
            stream_file(i:i) = stream_file_ptr(i)
        end do

        call cdeps_inline_init(gcomp, clock, mesh, trim(stream_file), f_rc)
        rc = f_rc
    end subroutine

    subroutine aces_cdeps_advance(clock_c, rc) bind(c, name="aces_cdeps_advance")
        type(c_ptr), value :: clock_c
        integer(c_int), intent(out) :: rc

        type(ESMF_Clock) :: clock
        integer :: f_rc

        clock = ESMF_ClockFromC(clock_c)
        call cdeps_inline_advance(clock, f_rc)
        rc = f_rc
    end subroutine

    subroutine aces_cdeps_get_ptr(stream_idx, fldname_c, data_ptr_c, rc) bind(c, name="aces_cdeps_get_ptr")
        integer(c_int), value :: stream_idx
        type(c_ptr), value :: fldname_c
        type(c_ptr), intent(out) :: data_ptr_c
        integer(c_int), intent(out) :: rc

        character(kind=c_char), pointer :: fldname_ptr(:)
        character(len=ESMF_MAXSTR) :: fldname
        real(ESMF_KIND_R8), pointer :: data_ptr(:)
        integer :: i, f_rc

        call c_f_pointer(fldname_c, fldname_ptr, [ESMF_MAXSTR])
        fldname = ""
        do i = 1, ESMF_MAXSTR
            if (fldname_ptr(i) == c_null_char) exit
            fldname(i:i) = fldname_ptr(i)
        end do

        call cdeps_get_field_ptr(int(stream_idx), trim(fldname), data_ptr, f_rc)
        rc = f_rc

        if (f_rc == ESMF_SUCCESS .and. associated(data_ptr)) then
            data_ptr_c = c_loc(data_ptr(1))
        else
            data_ptr_c = c_null_ptr
        end if
    end subroutine

end module
