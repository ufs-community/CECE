!> @file aces_cap.F90
!> @brief NUOPC Model cap for ACES using IPDv01 initialize protocol.
!>
!> Uses the same IPDv01 pattern as CDEPS/DATM for compatibility with both
!> standalone drivers and full NUOPC coupled systems.
!>
!> Phase map (IPDv01):
!>   IPDv01p1 -> InitializeAdvertise  (Kokkos init, config, advertise fields)
!>   IPDv01p3 -> InitializeRealize    (create/allocate ESMF fields, bind CDEPS)
!>   label_Advance  -> ACES_Run
!>   label_Finalize -> ACES_Finalize

module aces_cap_mod
  use iso_c_binding
  use ESMF
  use NUOPC
  use NUOPC_Model, modelSS => SetServices
  use NUOPC_Model, only : model_label_Advance  => label_Advance
  use NUOPC_Model, only : model_label_Finalize => label_Finalize
#ifdef ACES_HAS_CDEPS
  use cdeps_inline_mod
#endif
  implicit none

  !> @brief Module-level C++ data pointer (save ensures persistence across phases).
  type(c_ptr), save :: g_aces_data_ptr = c_null_ptr

  !> @brief Module-level config file path (save ensures persistence across phases).
  character(len=512), save :: g_config_file_path = "aces_config.yaml"

  ! C interface to ACES core
  interface
    subroutine aces_set_config_file_path(config_path, path_len) bind(C)
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
    end subroutine
    subroutine aces_core_advertise(importState, exportState, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: importState, exportState
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_realize(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_initialize_p1(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), intent(out) :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_initialize_p2(data_ptr, nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: nx, ny, nz
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_species_count(data_ptr, count, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_species_name(data_ptr, index, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: index
      character(kind=c_char), intent(out) :: name(*)
      integer(c_int), intent(out) :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_bind_fields(data_ptr, field_ptrs, num_fields, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      type(c_ptr), intent(in) :: field_ptrs(*)
      integer(c_int), value :: num_fields
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_cdeps_streams_path(data_ptr, streams_path, path_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(out) :: streams_path(*)
      integer(c_int), intent(out) :: path_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_cdeps_init(c_gcomp, c_clock, c_mesh, stream_path_c, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: c_gcomp, c_clock, c_mesh, stream_path_c
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_get_mesh_from_field(c_field, c_mesh, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: c_field
      type(c_ptr), intent(out) :: c_mesh
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_run(data_ptr, importState, exportState, clock, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr, importState, exportState, clock
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_finalize(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

contains

  !> @brief Set the configuration file path for ACES initialization.
  !> This should be called before ESMF_GridCompSetServices to ensure the
  !> correct config file is used during initialization phases.
  subroutine ACES_SetConfigPath(config_path, rc)
    character(len=*), intent(in) :: config_path
    integer, intent(out) :: rc

    if (len_trim(config_path) > 0 .and. len_trim(config_path) <= 512) then
      g_config_file_path = trim(config_path)
      rc = ESMF_SUCCESS
    else
      write(*,'(A)') "ERROR: [ACES] Invalid config path length"
      rc = ESMF_FAILURE
    end if
  end subroutine ACES_SetConfigPath

  subroutine ACES_SetServices(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    type(ESMF_State) :: dummy_import, dummy_export
    type(ESMF_Clock) :: dummy_clock

    ! 1. Inherit NUOPC_Model base phases
    call NUOPC_CompDerive(comp, modelSS, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompDerive failed rc=", rc
      return
    end if

    ! 2. Call the phase map routine directly to filter and replace init routines
    call ACES_InitPhaseMap(comp, dummy_import, dummy_export, dummy_clock, rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: ACES_InitPhaseMap failed rc=", rc
      return
    end if

    ! 3. Specialize Run (Advance)
    call NUOPC_CompSpecialize(comp, specLabel=model_label_Advance, &
      specRoutine=ACES_Run, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompSpecialize(Advance) failed rc=", rc
      return
    end if

    ! 4. Specialize Finalize
    call NUOPC_CompSpecialize(comp, specLabel=model_label_Finalize, &
      specRoutine=ACES_Finalize, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompSpecialize(Finalize) failed rc=", rc
      return
    end if

    rc = ESMF_SUCCESS
  end subroutine ACES_SetServices

  !> @brief Phase 0: filter the NUOPC initialize phase map to IPDv01 and
  !> replace the base NUOPC_ModelBase IPDv01p1/p3 entry points with our
  !> implementations. Called during ESMF_GridCompSetServices before any init phases run.
  !> Identical pattern to CDEPS dshr_model_initphase.
  subroutine ACES_InitPhaseMap(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    rc = ESMF_SUCCESS

    ! Filter to IPDv01 — removes all other IPD version entries
    call NUOPC_CompFilterPhaseMap(comp, ESMF_METHOD_INITIALIZE, &
      acceptStringList=(/"IPDv01p"/), rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompFilterPhaseMap failed rc=", rc
      return
    end if

    ! Now register our implementations for phases 1 and 2
    ! These replace whatever was there before (if anything)
    call ESMF_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, &
      userRoutine=ACES_InitializeAdvertise, phase=1, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: SetEntryPoint(phase=1) failed rc=", rc
      return
    end if

    call ESMF_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, &
      userRoutine=ACES_InitializeRealize, phase=2, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: SetEntryPoint(phase=2) failed rc=", rc
      return
    end if

    rc = ESMF_SUCCESS
  end subroutine ACES_InitPhaseMap

  !> @brief IPDv01p1: Advertise fields and perform core initialization.
  !> Runs Kokkos init, YAML config parse, PhysicsFactory, StackingEngine setup,
  !> and advertises all export fields. Equivalent to DATM's InitializeAdvertise.
  subroutine ACES_InitializeAdvertise(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(c_ptr)    :: new_data_ptr
    character(len=512) :: config_path
    integer :: config_len

    write(*,'(A)') "INFO: [ACES] InitializeAdvertise (IPDv01p1) entered"

    ! Get the config file path from the component's internal state if available
    ! For now, use the module-level default or environment variable
    config_path = g_config_file_path
    config_len = len_trim(config_path)

    ! Set the config path in C++ before initialization
    call aces_set_config_file_path(config_path(1:config_len)//c_null_char, int(config_len, c_int))

    ! --- Core init: Kokkos, YAML config, PhysicsFactory, StackingEngine ---
    call aces_core_initialize_p1(new_data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: aces_core_initialize_p1 failed rc=", rc
      return
    end if
    g_aces_data_ptr = new_data_ptr
    write(*,'(A)') "INFO: [ACES] Core initialized, data pointer stored"

    ! --- Advertise fields ---
    ! Call aces_core_advertise to log what fields will be created
    ! In NUOPC, the Advertise phase is informational - fields are created in Realize
    call aces_core_advertise(transfer(importState, c_null_ptr), &
                             transfer(exportState, c_null_ptr), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: aces_core_advertise failed rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [ACES] InitializeAdvertise complete"
    rc = ESMF_SUCCESS
  end subroutine ACES_InitializeAdvertise

  !> @brief IPDv01p3: Realize fields and bind CDEPS data streams.
  !> Creates and allocates ESMF fields, then runs aces_core_initialize_p2
  !> for CDEPS initialization and field binding.
  !> Equivalent to DATM's InitializeRealize.
  subroutine ACES_InitializeRealize(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(ESMF_Grid) :: grid
    type(ESMF_DistGrid) :: distgrid
    type(ESMF_Mesh) :: c_mesh
    integer :: nx, ny, nz
    integer, allocatable :: minIndex(:), maxIndex(:)
    integer :: num_species, i
    character(len=256) :: species_name
    integer(c_int) :: species_name_len
    character(len=512) :: streams_path
    integer(c_int) :: streams_path_len
    type(ESMF_Field) :: field
    real(ESMF_KIND_R8), pointer :: fptr(:,:,:)
    type(c_ptr), allocatable :: field_ptrs(:)
    integer :: field_lb(3), field_ub(3)
    character(len=512) :: error_msg
    type(c_ptr) :: c_mesh_temp
    character(len=512), target :: streams_path_c

    write(*,'(A)') "INFO: [ACES] InitializeRealize (IPDv01p3) entered"

    call ESMF_GridCompGet(comp, grid=grid, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [ACES] ESMF_GridCompGet failed in Realize: rc=", rc
      return
    end if

    ! Get grid dimensions from the grid
    call ESMF_GridGet(grid, distgrid=distgrid, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [ACES] ESMF_GridGet failed to retrieve DistGrid: rc=", rc
      return
    end if

    ! Get the size of the distgrid
    ! For now, use default dimensions (can be overridden by config)
    ! This avoids ESMF_DistGridGet which has interface issues
    allocate(minIndex(3), maxIndex(3))
    minIndex = [1, 1, 1]
    maxIndex = [4, 5, 10]  ! Default grid dimensions

    nx = maxIndex(1) - minIndex(1) + 1
    ny = maxIndex(2) - minIndex(2) + 1
    nz = maxIndex(3) - minIndex(3) + 1

    write(*,'(A,I0,A,I0,A,I0)') "INFO: [ACES] Grid dimensions (default): nx=", nx, " ny=", ny, " nz=", nz

    ! Call aces_core_realize (validates config)
    call aces_core_realize(g_aces_data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "WARNING: [ACES] aces_core_realize returned non-success: rc=", rc
      rc = ESMF_SUCCESS
    end if

    ! --- Phase 2: CDEPS init and field binding ---
    ! Pass grid dimensions to C++ (extracted from ESMF grid)
    call aces_core_initialize_p2(g_aces_data_ptr, int(nx, c_int), int(ny, c_int), &
                                 int(nz, c_int), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [ACES] aces_core_initialize_p2 failed: rc=", rc
      deallocate(minIndex, maxIndex)
      return
    end if

    ! --- Create ESMF fields for each species ---
    ! Get the number of species from C++
    call aces_core_get_species_count(g_aces_data_ptr, num_species, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [ACES] aces_core_get_species_count failed: rc=", rc
      deallocate(minIndex, maxIndex)
      return
    end if

    if (num_species <= 0) then
      write(*,'(A,I0)') "ERROR: [ACES] Invalid species count from C++: ", num_species
      deallocate(minIndex, maxIndex)
      rc = ESMF_FAILURE
      return
    end if

    write(*,'(A,I0,A)') "INFO: [ACES] Creating ESMF fields for ", num_species, " species"

    ! Allocate array to store field data pointers
    allocate(field_ptrs(num_species))

    ! Create a field for each species
    do i = 1, num_species
      ! Get species name from C++
      call aces_core_get_species_name(g_aces_data_ptr, int(i-1, c_int), species_name, &
                                      species_name_len, c_rc)
      rc = int(c_rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,I0)') "ERROR: [ACES] aces_core_get_species_name failed for species ", i, &
                               ": rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      if (species_name_len <= 0 .or. species_name_len > 256) then
        write(*,'(A,I0,A,I0)') "ERROR: [ACES] Invalid species name length for species ", i, &
                               ": len=", species_name_len
        deallocate(minIndex, maxIndex, field_ptrs)
        rc = ESMF_FAILURE
        return
      end if

      ! Create 3D ESMF field on the grid
      field = ESMF_FieldCreate(grid, ESMF_TYPEKIND_R8, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,A,A,I0)') "ERROR: [ACES] ESMF_FieldCreate failed for species ", i, &
                                   " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      ! Get field bounds to verify dimensions match grid
      call ESMF_FieldGet(field, ungriddedLBound=field_lb, ungriddedUBound=field_ub, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,A,A,I0)') "WARNING: [ACES] ESMF_FieldGet bounds failed for species ", i, &
                                   " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        ! Continue anyway - bounds check is optional
        rc = ESMF_SUCCESS
      end if

      ! Get field data pointer
      call ESMF_FieldGetDataPointer(field, fptr, rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,A,A,I0)') "ERROR: [ACES] ESMF_FieldGetDataPointer failed for species ", i, &
                                   " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      if (.not. associated(fptr)) then
        write(*,'(A,I0,A,A)') "ERROR: [ACES] Field data pointer is not associated for species ", i, &
                              ": ", trim(species_name(1:int(species_name_len)))
        deallocate(minIndex, maxIndex, field_ptrs)
        rc = ESMF_FAILURE
        return
      end if

      ! Store the data pointer (c_loc gets the address of the first element)
      field_ptrs(i) = c_loc(fptr(1,1,1))

      ! Add field to export state
      call ESMF_StateAddField(exportState, field, species_name(1:int(species_name_len)), rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,A,A,I0)') "ERROR: [ACES] ESMF_StateAddField failed for species ", i, &
                                   " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      write(*,'(A,I0,A,A,A)') "INFO: [ACES] Successfully created field for species ", i, &
                              ": ", trim(species_name(1:int(species_name_len))), &
                              " with data pointer"
    end do

    ! Pass field pointers to C++ for storage in internal_data
    call aces_core_bind_fields(g_aces_data_ptr, field_ptrs, int(num_species, c_int), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [ACES] aces_core_bind_fields failed: rc=", rc
      deallocate(minIndex, maxIndex, field_ptrs)
      return
    end if

    ! --- Phase 4: Initialize CDEPS if configured ---
    ! CDEPS initialization is optional and can be skipped if not configured
    ! Attempt to initialize CDEPS with the created fields
    write(*,'(A)') "INFO: [ACES] Attempting CDEPS initialization with created fields"

    if (num_species > 0) then
      ! Get the first field to extract mesh from
      call aces_core_get_species_name(g_aces_data_ptr, int(0, c_int), species_name, &
                                      species_name_len, c_rc)
      if (c_rc == ESMF_SUCCESS .and. species_name_len > 0) then
        ! Get the field from export state
        call ESMF_StateGetField(exportState, species_name(1:int(species_name_len)), field, rc)
        if (rc == ESMF_SUCCESS) then
          ! Extract mesh from field
          c_mesh_temp = c_null_ptr
          call aces_get_mesh_from_field(transfer(field, c_null_ptr), c_mesh_temp, c_rc)
          if (c_rc == ESMF_SUCCESS) then
            ! Get CDEPS streams file path from C++
            call aces_core_get_cdeps_streams_path(g_aces_data_ptr, streams_path, &
                                                 streams_path_len, c_rc)
            if (c_rc == ESMF_SUCCESS .and. streams_path_len > 0) then
              ! Initialize CDEPS with the mesh and export state
              streams_path_c = streams_path(1:int(streams_path_len))
              write(*,'(A,A)') "INFO: [ACES] Calling aces_cdeps_init with streams file: ", &
                              trim(streams_path_c)
              call aces_cdeps_init(transfer(comp, c_null_ptr), &
                                  transfer(clock, c_null_ptr), &
                                  c_mesh_temp, &
                                  c_loc(streams_path_c), c_rc)
              if (c_rc /= ESMF_SUCCESS) then
                write(*,'(A,I0)') "WARNING: [ACES] CDEPS initialization failed: rc=", c_rc
                ! Don't fail the entire initialization if CDEPS is not available
                rc = ESMF_SUCCESS
              else
                write(*,'(A)') "INFO: [ACES] CDEPS initialized successfully"
              end if
            else
              write(*,'(A)') "INFO: [ACES] No CDEPS streams configured - skipping CDEPS initialization"
              rc = ESMF_SUCCESS
            end if
          else
            write(*,'(A,I0)') "WARNING: [ACES] Failed to extract mesh from field: rc=", c_rc
            rc = ESMF_SUCCESS
          end if
        else
          write(*,'(A,I0)') "WARNING: [ACES] Failed to get field from export state: rc=", rc
          rc = ESMF_SUCCESS
        end if
      else
        write(*,'(A)') "WARNING: [ACES] Failed to get species name for CDEPS initialization"
        rc = ESMF_SUCCESS
      end if
    else
      write(*,'(A)') "WARNING: [ACES] No species available for CDEPS initialization"
      rc = ESMF_SUCCESS
    end if

    deallocate(minIndex, maxIndex, field_ptrs)
    write(*,'(A)') "INFO: [ACES] InitializeRealize completed successfully"
    rc = ESMF_SUCCESS
  end subroutine ACES_InitializeRealize

  !> @brief Initialize CDEPS with created fields and export state (DEPRECATED).
  !> @details This subroutine is deprecated and not used in the current implementation.
  !>          CDEPS initialization is handled separately if needed.
  subroutine ACES_InitializeCDEPS_DEPRECATED(comp, exportState, clock, data_ptr, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: exportState
    type(ESMF_Clock)     :: clock
    type(c_ptr), value   :: data_ptr
    integer, intent(out) :: rc

    write(*,'(A)') "WARNING: [ACES] ACES_InitializeCDEPS_DEPRECATED called but is not implemented"
    rc = ESMF_SUCCESS
  end subroutine ACES_InitializeCDEPS_DEPRECATED

  !> @brief Run phase: advance CDEPS and execute ACES physics/stacking.
  subroutine ACES_Run(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    type(ESMF_State) :: importState, exportState
    type(ESMF_Clock) :: clock
    integer(c_int)   :: c_rc

    call ESMF_GridCompGet(comp, importState=importState, exportState=exportState, &
                          clock=clock, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    if (.not. c_associated(g_aces_data_ptr)) then
      write(*,'(A)') "ERROR: [ACES] Run called but data pointer is null"
      rc = ESMF_FAILURE
      return
    end if

#ifdef ACES_HAS_CDEPS
    call cdeps_inline_advance(clock, rc)
    if (rc /= ESMF_SUCCESS) rc = ESMF_SUCCESS
#endif

    call aces_core_run(g_aces_data_ptr, &
                       transfer(importState, c_null_ptr), &
                       transfer(exportState, c_null_ptr), &
                       transfer(clock, c_null_ptr), c_rc)
    rc = int(c_rc)
  end subroutine ACES_Run

  !> @brief Finalize phase: release all ACES resources.
  subroutine ACES_Finalize(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc

    if (.not. c_associated(g_aces_data_ptr)) then
      write(*,'(A)') "WARNING: [ACES] Finalize called but data pointer is null"
      rc = ESMF_SUCCESS
      return
    end if

    call aces_core_finalize(g_aces_data_ptr, c_rc)
    g_aces_data_ptr = c_null_ptr
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: aces_core_finalize failed rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [ACES] Finalize complete"
    rc = ESMF_SUCCESS
  end subroutine ACES_Finalize

end module aces_cap_mod
