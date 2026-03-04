module aces_cdeps_bridge_mod
  use iso_c_binding
  use ESMF
  use cdeps_inline_mod
  implicit none

  ! Persistent native Fortran objects to avoid leaks and maintain state
  type(ESMF_GridComp), save :: f_gcomp
  type(ESMF_Clock),    save :: f_clock_init
  logical,             save :: is_initialized = .false.

contains

  subroutine aces_cdeps_init(c_gcomp, c_clock, c_mesh, stream_path_c, rc) &
                            bind(C, name="aces_cdeps_init")
    type(c_ptr), value                :: c_gcomp
    type(c_ptr), value                :: c_clock
    type(c_ptr), value                :: c_mesh
    type(c_ptr), value                :: stream_path_c
    integer(c_int), intent(out)       :: rc

    ! Local Fortran ESMF types
    type(ESMF_Mesh)     :: f_mesh
    character(kind=c_char), pointer :: stream_path_ptr(:)
    character(len=256)  :: f_stream_path
    integer             :: i, f_rc

    ! 1. Convert C strings to Fortran strings
    call c_f_pointer(stream_path_c, stream_path_ptr, [256])
    i = 1
    f_stream_path = ""
    do while (stream_path_ptr(i) /= c_null_char .and. i <= 256)
       f_stream_path(i:i) = stream_path_ptr(i)
       i = i + 1
    end do

    ! 2. Reconstitute native Fortran objects from C handles.
    ! ESMF Fortran handles are structures requiring a validity flag at the second 8-byte offset.

    ! Reconstitute GridComp handle. Flag: 82949521
    f_gcomp = transfer([transfer(c_gcomp, 0_8), 82949521_8], f_gcomp)

    ! Reconstitute Mesh handle. Flag: 82949521
    f_mesh  = transfer([transfer(c_mesh,  0_8), 82949521_8], f_mesh)
    ! Clock/Field flag: 76838410
    f_clock_init = transfer([transfer(c_clock, 0_8), 76838410_8], f_clock_init)

    is_initialized = .true.

    ! 3. Call the high-level Fortran interface
    print *, "ACES Bridge: Initializing CDEPS with reconstituted handles."
    print *, "ACES Bridge: Stream file: ", trim(f_stream_path)
    print *, "ACES Bridge: GridComp valid=", ESMF_GridCompIsCreated(f_gcomp)
    print *, "ACES Bridge: Clock valid=", ESMF_ClockIsCreated(f_clock_init)

    call cdeps_inline_init(f_gcomp, f_clock_init, f_mesh, trim(f_stream_path), f_rc)
    print *, "ACES Bridge: CDEPS initialization returned RC: ", f_rc

    rc = int(f_rc, c_int)
  end subroutine

  subroutine aces_cdeps_advance(c_clock, rc) bind(C, name="aces_cdeps_advance")
    type(c_ptr), value          :: c_clock
    integer(c_int), intent(out) :: rc

    type(ESMF_Clock) :: f_clock
    integer          :: f_rc

    ! Reconstitute Clock handle.
    f_clock = transfer([transfer(c_clock, 0_8), 76838410_8], f_clock)

    call cdeps_inline_advance(f_clock, f_rc)

    rc = int(f_rc, c_int)
  end subroutine

  subroutine aces_cdeps_get_ptr(stream_idx, fldname_c, data_ptr_c, rc) bind(C, name="aces_cdeps_get_ptr")
    integer(c_int), value       :: stream_idx
    type(c_ptr), value          :: fldname_c
    type(c_ptr), intent(out)    :: data_ptr_c
    integer(c_int), intent(out) :: rc

    character(kind=c_char), pointer :: fldname_ptr(:)
    character(len=256) :: fldname
    real(ESMF_KIND_R8), pointer :: data_ptr(:)
    integer :: i, f_rc

    ! Convert C string
    call c_f_pointer(fldname_c, fldname_ptr, [256])
    i = 1
    fldname = ""
    do while (fldname_ptr(i) /= c_null_char .and. i <= 256)
       fldname(i:i) = fldname_ptr(i)
       i = i + 1
    end do

    call cdeps_get_field_ptr(int(stream_idx), trim(fldname), data_ptr, f_rc)
    rc = int(f_rc, c_int)

    if (f_rc == ESMF_SUCCESS .and. associated(data_ptr)) then
        data_ptr_c = c_loc(data_ptr(1))
    else
        data_ptr_c = c_null_ptr
    end if
  end subroutine

  subroutine aces_cdeps_finalize() bind(C, name="aces_cdeps_finalize")
    if (is_initialized) then
        print *, "ACES Bridge: Finalizing."
        ! Note: cdeps_inline_mod currently does not have a finalize call.
        is_initialized = .false.
    end if
  end subroutine

  subroutine aces_get_mesh_from_field(c_field, c_mesh, rc) bind(C, name="aces_get_mesh_from_field")
    type(c_ptr), value          :: c_field
    type(c_ptr), intent(out)    :: c_mesh
    integer(c_int), intent(out) :: rc

    type(ESMF_Field) :: f_field
    type(ESMF_Mesh)  :: f_mesh
    type(ESMF_Grid)  :: f_grid
    integer          :: f_rc

    ! Reconstitute Field handle. Validity flag for Field/Clock is 76838410.
    f_field = transfer([transfer(c_field, 0_8), 76838410_8], f_field)

    f_rc = ESMF_SUCCESS
    call ESMF_FieldGet(f_field, mesh=f_mesh, rc=f_rc)

    if (f_rc /= ESMF_SUCCESS .or. .not. ESMF_MeshIsCreated(f_mesh)) then
        call ESMF_FieldGet(f_field, grid=f_grid, rc=f_rc)
        if (f_rc == ESMF_SUCCESS .and. ESMF_GridIsCreated(f_grid)) then
            f_mesh = ESMF_MeshCreate(f_grid, rc=f_rc)
        end if
    end if

    rc = int(f_rc, c_int)
    if (f_rc == ESMF_SUCCESS .and. ESMF_MeshIsCreated(f_mesh)) then
        c_mesh = transfer(f_mesh, c_null_ptr)
    else
        c_mesh = c_null_ptr
    end if
  end subroutine

end module
