!> @file test_helpers.F90
!> @brief Fortran test helpers for creating ESMF objects that require
!>        Fortran-level API access (e.g. ESMF_CONTEXT_PARENT_VM).
!>
!> ESMC_GridCompCreate always creates a new MPI sub-communicator, which
!> crashes in the JCSDA Docker single-process test environment.
!> ESMF_GridCompCreate with contextflag=ESMF_CONTEXT_PARENT_VM reuses
!> the parent VM and works correctly.

module test_helpers_mod
  use iso_c_binding
  use ESMF
  implicit none

contains

  !> @brief Create an ESMF_GridComp reusing the parent VM context.
  !>
  !> Returns the GridComp as a C pointer via transfer() so C++ tests
  !> can pass it to cece_core_initialize_p2 and other C bridges.
  !>
  !> @param name  Component name (null-terminated C string)
  !> @param clock Clock C pointer (already created by C++ test)
  !> @param gcomp_ptr Output: GridComp as opaque C pointer
  !> @param rc    Return code (ESMF_SUCCESS on success)
  subroutine test_create_gridcomp(name, clock_ptr, gcomp_ptr, rc) bind(C)
    character(kind=c_char), intent(in) :: name(*)
    type(c_ptr), value,     intent(in) :: clock_ptr
    type(c_ptr),           intent(out) :: gcomp_ptr
    integer(c_int),        intent(out) :: rc

    type(ESMF_GridComp) :: gcomp
    type(ESMF_Clock)    :: clock
    integer             :: local_rc
    integer             :: name_len
    character(len=256)  :: f_name

    ! Convert C string to Fortran string
    name_len = 0
    do while (name(name_len+1) /= c_null_char .and. name_len < 255)
      name_len = name_len + 1
      f_name(name_len:name_len) = name(name_len)
    end do

    ! Reconstitute clock from C pointer
    clock = transfer(clock_ptr, clock)

    ! Create GridComp with PARENT_VM to avoid MPI sub-communicator creation
    gcomp = ESMF_GridCompCreate( &
      name        = f_name(1:name_len), &
      contextflag = ESMF_CONTEXT_PARENT_VM, &
      rc          = local_rc)

    if (local_rc /= ESMF_SUCCESS) then
      gcomp_ptr = c_null_ptr
      rc = int(local_rc)
      return
    end if

    gcomp_ptr = transfer(gcomp, c_null_ptr)
    rc = int(ESMF_SUCCESS)
  end subroutine

  !> @brief Destroy an ESMF_GridComp given as a C pointer.
  subroutine test_destroy_gridcomp(gcomp_ptr, rc) bind(C)
    type(c_ptr), value,  intent(in) :: gcomp_ptr
    integer(c_int),     intent(out) :: rc

    type(ESMF_GridComp) :: gcomp
    integer             :: local_rc

    gcomp = transfer(gcomp_ptr, gcomp)
    call ESMF_GridCompDestroy(gcomp, rc=local_rc)
    rc = int(local_rc)
  end subroutine

end module test_helpers_mod
