module aces_cdeps_bridge_mod
  use iso_c_binding
  use ESMF
  use cdeps_inline_mod
  implicit none

  ! Persistent native Fortran objects to avoid leaks and maintain state
  type(ESMF_GridComp), save :: f_gcomp
  type(ESMF_Clock),    save :: f_clock_init
  logical,             save :: is_initialized = .false.

  ! ESMF validity flags (from ESMF internal implementation)
  integer(kind=8), parameter :: ESMF_GRIDCOMP_VALID = 82949521_8
  integer(kind=8), parameter :: ESMF_MESH_VALID = 82949521_8
  integer(kind=8), parameter :: ESMF_CLOCK_VALID = 76838410_8
  integer(kind=8), parameter :: ESMF_FIELD_VALID = 76838410_8

contains

  !> @brief Initialize CDEPS with robust handle reconstitution and error handling
  !> @param[in] c_gcomp C pointer to ESMF GridComp
  !> @param[in] c_clock C pointer to ESMF Clock
  !> @param[in] c_mesh C pointer to ESMF Mesh
  !> @param[in] stream_path_c C pointer to streams file path
  !> @param[out] rc Return code (0=success, non-zero=error)
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
    character(len=512)  :: f_stream_path
    integer             :: i, f_rc
    logical             :: file_exists

    ! Initialize return code
    rc = ESMF_FAILURE

    ! 1. Validate input pointers
    if (.not. c_associated(c_gcomp)) then
       print *, "ERROR [aces_cdeps_init]: GridComp pointer is null"
       print *, "CORRECTIVE ACTION: Ensure ESMF GridComp is created before calling aces_cdeps_init"
       return
    end if

    if (.not. c_associated(c_clock)) then
       print *, "ERROR [aces_cdeps_init]: Clock pointer is null"
       print *, "CORRECTIVE ACTION: Ensure ESMF Clock is created before calling aces_cdeps_init"
       return
    end if

    if (.not. c_associated(c_mesh)) then
       print *, "ERROR [aces_cdeps_init]: Mesh pointer is null"
       print *, "CORRECTIVE ACTION: Ensure ESMF Mesh is created or extracted from field before calling aces_cdeps_init"
       return
    end if

    if (.not. c_associated(stream_path_c)) then
       print *, "ERROR [aces_cdeps_init]: Stream path pointer is null"
       print *, "CORRECTIVE ACTION: Provide valid streams file path"
       return
    end if

    ! 2. Convert C string to Fortran string with bounds checking
    call c_f_pointer(stream_path_c, stream_path_ptr, [512])
    i = 1
    f_stream_path = ""
    do while (i <= 512)
       if (stream_path_ptr(i) == c_null_char) exit
       f_stream_path(i:i) = stream_path_ptr(i)
       i = i + 1
    end do

    if (len_trim(f_stream_path) == 0) then
       print *, "ERROR [aces_cdeps_init]: Stream path is empty"
       print *, "CORRECTIVE ACTION: Provide non-empty streams file path"
       return
    end if

    ! 3. Validate stream file exists and is readable
    inquire(file=trim(f_stream_path), exist=file_exists)
    if (.not. file_exists) then
       print *, "ERROR [aces_cdeps_init]: Stream file does not exist: ", trim(f_stream_path)
       print *, "CORRECTIVE ACTION: Verify file path is correct and file is accessible"
       return
    end if

    ! 4. Reconstitute native Fortran objects from C handles with correct validity flags
    ! ESMF Fortran handles are structures requiring a validity flag at the second 8-byte offset.

    ! Reconstitute GridComp handle (validity flag: 82949521)
    f_gcomp = transfer([transfer(c_gcomp, 0_8), ESMF_GRIDCOMP_VALID], f_gcomp)

    ! Reconstitute Mesh handle (validity flag: 82949521)
    f_mesh  = transfer([transfer(c_mesh,  0_8), ESMF_MESH_VALID], f_mesh)

    ! Reconstitute Clock handle (validity flag: 76838410)
    f_clock_init = transfer([transfer(c_clock, 0_8), ESMF_CLOCK_VALID], f_clock_init)

    ! 5. Validate reconstituted ESMF handles
    if (.not. ESMF_GridCompIsCreated(f_gcomp)) then
       print *, "ERROR [aces_cdeps_init]: GridComp handle reconstitution failed"
       print *, "CORRECTIVE ACTION: Verify GridComp was properly created in calling code"
       print *, "DEBUG INFO: Expected validity flag: ", ESMF_GRIDCOMP_VALID
       return
    end if

    if (.not. ESMF_ClockIsCreated(f_clock_init)) then
       print *, "ERROR [aces_cdeps_init]: Clock handle reconstitution failed"
       print *, "CORRECTIVE ACTION: Verify Clock was properly created in calling code"
       print *, "DEBUG INFO: Expected validity flag: ", ESMF_CLOCK_VALID
       return
    end if

    if (.not. ESMF_MeshIsCreated(f_mesh)) then
       print *, "ERROR [aces_cdeps_init]: Mesh handle reconstitution failed"
       print *, "CORRECTIVE ACTION: Verify Mesh was properly created or extracted from field"
       print *, "DEBUG INFO: Expected validity flag: ", ESMF_MESH_VALID
       return
    end if

    ! 6. Call CDEPS initialization with validated handles
    print *, "INFO [aces_cdeps_init]: Initializing CDEPS with validated handles"
    print *, "INFO [aces_cdeps_init]: Stream file: ", trim(f_stream_path)
    print *, "INFO [aces_cdeps_init]: GridComp valid: ", ESMF_GridCompIsCreated(f_gcomp)
    print *, "INFO [aces_cdeps_init]: Clock valid: ", ESMF_ClockIsCreated(f_clock_init)
    print *, "INFO [aces_cdeps_init]: Mesh valid: ", ESMF_MeshIsCreated(f_mesh)

    call cdeps_inline_init(f_gcomp, f_clock_init, f_mesh, trim(f_stream_path), f_rc)

    if (f_rc /= ESMF_SUCCESS) then
       print *, "ERROR [aces_cdeps_init]: CDEPS initialization failed with RC: ", f_rc
       print *, "CORRECTIVE ACTION: Check streams file format and NetCDF data files"
       print *, "CORRECTIVE ACTION: Verify all variables in streams file exist in NetCDF files"
       print *, "CORRECTIVE ACTION: Check CDEPS log output for detailed error messages"
       rc = int(f_rc, c_int)
       return
    end if

    print *, "INFO [aces_cdeps_init]: CDEPS initialization successful"
    is_initialized = .true.
    rc = int(ESMF_SUCCESS, c_int)

  end subroutine aces_cdeps_init

  !> @brief Advance CDEPS to current clock time with error handling
  !> @param[in] c_clock C pointer to ESMF Clock
  !> @param[out] rc Return code (0=success, non-zero=error)
  subroutine aces_cdeps_advance(c_clock, rc) bind(C, name="aces_cdeps_advance")
    type(c_ptr), value          :: c_clock
    integer(c_int), intent(out) :: rc

    type(ESMF_Clock) :: f_clock
    integer          :: f_rc

    ! Initialize return code
    rc = ESMF_FAILURE

    ! 1. Validate CDEPS is initialized
    if (.not. is_initialized) then
       print *, "ERROR [aces_cdeps_advance]: CDEPS not initialized"
       print *, "CORRECTIVE ACTION: Call aces_cdeps_init before aces_cdeps_advance"
       return
    end if

    ! 2. Validate clock pointer
    if (.not. c_associated(c_clock)) then
       print *, "ERROR [aces_cdeps_advance]: Clock pointer is null"
       print *, "CORRECTIVE ACTION: Ensure ESMF Clock is valid before calling aces_cdeps_advance"
       return
    end if

    ! 3. Reconstitute Clock handle with correct validity flag
    f_clock = transfer([transfer(c_clock, 0_8), ESMF_CLOCK_VALID], f_clock)

    ! 4. Validate reconstituted handle
    if (.not. ESMF_ClockIsCreated(f_clock)) then
       print *, "ERROR [aces_cdeps_advance]: Clock handle reconstitution failed"
       print *, "CORRECTIVE ACTION: Verify Clock was properly created in calling code"
       return
    end if

    ! 5. Call CDEPS advance
    call cdeps_inline_advance(f_clock, f_rc)

    if (f_rc /= ESMF_SUCCESS) then
       print *, "ERROR [aces_cdeps_advance]: CDEPS advance failed with RC: ", f_rc
       print *, "CORRECTIVE ACTION: Check CDEPS log for temporal interpolation errors"
       print *, "CORRECTIVE ACTION: Verify clock time is within data file time bounds"
       rc = int(f_rc, c_int)
       return
    end if

    rc = int(ESMF_SUCCESS, c_int)
  end subroutine aces_cdeps_advance

  !> @brief Get pointer to CDEPS field data with validation
  !> @param[in] stream_idx Stream index
  !> @param[in] fldname_c C pointer to field name string
  !> @param[out] data_ptr_c C pointer to field data
  !> @param[out] rc Return code (0=success, non-zero=error)
  subroutine aces_cdeps_get_ptr(stream_idx, fldname_c, data_ptr_c, rc) bind(C, name="aces_cdeps_get_ptr")
    integer(c_int), value       :: stream_idx
    type(c_ptr), value          :: fldname_c
    type(c_ptr), intent(out)    :: data_ptr_c
    integer(c_int), intent(out) :: rc

    character(kind=c_char), pointer :: fldname_ptr(:)
    character(len=256) :: fldname
    real(ESMF_KIND_R8), pointer :: data_ptr(:)
    integer :: i, f_rc

    ! Initialize outputs
    data_ptr_c = c_null_ptr
    rc = ESMF_FAILURE

    ! 1. Validate CDEPS is initialized
    if (.not. is_initialized) then
       print *, "ERROR [aces_cdeps_get_ptr]: CDEPS not initialized"
       print *, "CORRECTIVE ACTION: Call aces_cdeps_init before aces_cdeps_get_ptr"
       return
    end if

    ! 2. Validate field name pointer
    if (.not. c_associated(fldname_c)) then
       print *, "ERROR [aces_cdeps_get_ptr]: Field name pointer is null"
       print *, "CORRECTIVE ACTION: Provide valid field name string"
       return
    end if

    ! 3. Convert C string to Fortran string
    call c_f_pointer(fldname_c, fldname_ptr, [256])
    i = 1
    fldname = ""
    do while (i <= 256)
       if (fldname_ptr(i) == c_null_char) exit
       fldname(i:i) = fldname_ptr(i)
       i = i + 1
    end do

    if (len_trim(fldname) == 0) then
       print *, "ERROR [aces_cdeps_get_ptr]: Field name is empty"
       print *, "CORRECTIVE ACTION: Provide non-empty field name"
       return
    end if

    ! 4. Validate stream index
    if (stream_idx < 0) then
       print *, "ERROR [aces_cdeps_get_ptr]: Invalid stream index: ", stream_idx
       print *, "CORRECTIVE ACTION: Stream index must be non-negative"
       return
    end if

    ! 5. Get field pointer from CDEPS
    call cdeps_get_field_ptr(int(stream_idx), trim(fldname), data_ptr, f_rc)

    if (f_rc /= ESMF_SUCCESS) then
       print *, "ERROR [aces_cdeps_get_ptr]: Failed to get field pointer for: ", trim(fldname)
       print *, "ERROR [aces_cdeps_get_ptr]: Stream index: ", stream_idx, " RC: ", f_rc
       print *, "CORRECTIVE ACTION: Verify field name exists in streams configuration"
       print *, "CORRECTIVE ACTION: Verify stream index is correct"
       rc = int(f_rc, c_int)
       return
    end if

    ! 6. Validate returned pointer
    if (.not. associated(data_ptr)) then
       print *, "ERROR [aces_cdeps_get_ptr]: Field pointer is not associated: ", trim(fldname)
       print *, "CORRECTIVE ACTION: Verify CDEPS has read data for this field"
       print *, "CORRECTIVE ACTION: Call aces_cdeps_advance before accessing field data"
       rc = ESMF_FAILURE
       return
    end if

    ! 7. Return C pointer to data
    data_ptr_c = c_loc(data_ptr(1))
    rc = int(ESMF_SUCCESS, c_int)

  end subroutine aces_cdeps_get_ptr

  !> @brief Finalize CDEPS and clean up resources
  subroutine aces_cdeps_finalize() bind(C, name="aces_cdeps_finalize")
    if (is_initialized) then
        print *, "INFO [aces_cdeps_finalize]: Finalizing CDEPS bridge"
        ! Note: cdeps_inline_mod currently does not have a finalize call.
        ! This is a placeholder for future CDEPS finalization support.
        is_initialized = .false.
        print *, "INFO [aces_cdeps_finalize]: CDEPS bridge finalized successfully"
    else
        print *, "WARNING [aces_cdeps_finalize]: CDEPS was not initialized, nothing to finalize"
    end if
  end subroutine aces_cdeps_finalize

  !> @brief Extract mesh from ESMF field with fallback to grid
  !> @details Attempts to get mesh from field first. If mesh is not available,
  !>          falls back to getting grid and converting to mesh. Provides clear
  !>          error messages if neither exists.
  !> @param[in] c_field C pointer to ESMF Field
  !> @param[out] c_mesh C pointer to extracted ESMF Mesh
  !> @param[out] rc Return code (0=success, non-zero=error)
  subroutine aces_get_mesh_from_field(c_field, c_mesh, rc) bind(C, name="aces_get_mesh_from_field")
    type(c_ptr), value          :: c_field
    type(c_ptr), intent(out)    :: c_mesh
    integer(c_int), intent(out) :: rc

    type(ESMF_Field) :: f_field
    type(ESMF_Mesh)  :: f_mesh
    type(ESMF_Grid)  :: f_grid
    integer          :: f_rc, mesh_rc, grid_rc
    logical          :: has_mesh, has_grid

    ! Initialize outputs
    c_mesh = c_null_ptr
    rc = ESMF_FAILURE

    ! 1. Validate field pointer
    if (.not. c_associated(c_field)) then
       print *, "ERROR [aces_get_mesh_from_field]: Field pointer is null"
       print *, "CORRECTIVE ACTION: Ensure ESMF Field is created before calling aces_get_mesh_from_field"
       return
    end if

    ! 2. Reconstitute Field handle with correct validity flag
    f_field = transfer([transfer(c_field, 0_8), ESMF_FIELD_VALID], f_field)

    ! 3. Validate reconstituted field handle
    if (.not. ESMF_FieldIsCreated(f_field)) then
       print *, "ERROR [aces_get_mesh_from_field]: Field handle reconstitution failed"
       print *, "CORRECTIVE ACTION: Verify Field was properly created in calling code"
       print *, "DEBUG INFO: Expected validity flag: ", ESMF_FIELD_VALID
       return
    end if

    ! 4. Try to get mesh from field first
    print *, "INFO [aces_get_mesh_from_field]: Attempting to extract mesh from field"
    mesh_rc = ESMF_SUCCESS
    call ESMF_FieldGet(f_field, mesh=f_mesh, rc=mesh_rc)

    has_mesh = (mesh_rc == ESMF_SUCCESS .and. ESMF_MeshIsCreated(f_mesh))

    if (has_mesh) then
       print *, "INFO [aces_get_mesh_from_field]: Successfully extracted mesh from field"
       ! Convert Fortran mesh handle to C pointer
       c_mesh = transfer(f_mesh, c_null_ptr)
       rc = int(ESMF_SUCCESS, c_int)
       return
    end if

    ! 5. Mesh not available, try to get grid and convert
    print *, "INFO [aces_get_mesh_from_field]: Mesh not found, attempting to extract grid"
    grid_rc = ESMF_SUCCESS
    call ESMF_FieldGet(f_field, grid=f_grid, rc=grid_rc)

    has_grid = (grid_rc == ESMF_SUCCESS .and. ESMF_GridIsCreated(f_grid))

    if (has_grid) then
       print *, "INFO [aces_get_mesh_from_field]: Successfully extracted grid, converting to mesh"
       ! Create mesh from grid
       f_mesh = ESMF_MeshCreate(f_grid, rc=f_rc)

       if (f_rc /= ESMF_SUCCESS) then
          print *, "ERROR [aces_get_mesh_from_field]: Failed to create mesh from grid, RC: ", f_rc
          print *, "CORRECTIVE ACTION: Verify grid is properly initialized"
          print *, "CORRECTIVE ACTION: Check ESMF version supports grid-to-mesh conversion"
          rc = int(f_rc, c_int)
          return
       end if

       if (.not. ESMF_MeshIsCreated(f_mesh)) then
          print *, "ERROR [aces_get_mesh_from_field]: Mesh creation from grid failed validation"
          print *, "CORRECTIVE ACTION: Verify grid has valid coordinates and connectivity"
          return
       end if

       print *, "INFO [aces_get_mesh_from_field]: Successfully created mesh from grid"
       c_mesh = transfer(f_mesh, c_null_ptr)
       rc = int(ESMF_SUCCESS, c_int)
       return
    end if

    ! 6. Neither mesh nor grid available - report error
    print *, "ERROR [aces_get_mesh_from_field]: Field has neither mesh nor grid"
    print *, "ERROR [aces_get_mesh_from_field]: Mesh query RC: ", mesh_rc
    print *, "ERROR [aces_get_mesh_from_field]: Grid query RC: ", grid_rc
    print *, "CORRECTIVE ACTION: Ensure field is created with either a mesh or grid"
    print *, "CORRECTIVE ACTION: Verify field is fully realized before mesh extraction"

  end subroutine aces_get_mesh_from_field

end module aces_cdeps_bridge_mod
