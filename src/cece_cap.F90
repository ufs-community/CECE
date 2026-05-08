!> @file cece_cap.F90
!> @brief NUOPC Model cap for CECE using IPDv01 initialize protocol.
!>
!> Uses the same IPDv01 pattern as DATM for compatibility with both
!> standalone drivers and full NUOPC coupled systems.
!>
!> Phase map (IPDv01):
!>   IPDv01p1 -> InitializeAdvertise  (Kokkos init, config, advertise fields)
!>   IPDv01p3 -> InitializeRealize    (create/allocate ESMF fields, bind ingestor)
!>   label_Advance  -> CECE_Run
!>   label_Finalize -> CECE_Finalize

module cece_cap_mod
  use iso_c_binding
  use ESMF
  use NUOPC
  use NUOPC_Model, modelSS => SetServices
  use NUOPC_Model, only: model_label_Advance => label_Advance
  use NUOPC_Model, only: model_label_Finalize => label_Finalize
  use NUOPC_Model, only : model_label_Advance  => label_Advance
  use NUOPC_Model, only : model_label_Finalize => label_Finalize
  use tide_mod, only: tide_type, tide_init, tide_advance, tide_get_ptr, tide_finalize
  implicit none

  !> @brief Module-level C++ data pointer (save ensures persistence across phases).
  type(c_ptr), save :: g_cece_data_ptr = c_null_ptr

  !> @brief Module-level TIDE object
  type(tide_type), save :: g_tide
  logical, save :: g_tide_initialized = .false.

  !> @brief Module-level config file path (save ensures persistence across phases).
  character(len=512), save :: g_config_file_path = "cece_config.yaml"

  !> @brief Module-level step counter for output indexing.
  integer, save :: g_step_count = 0

  !> @brief Module-level start time (seconds since epoch) for elapsed time computation.
  real(c_double), save :: g_start_time_seconds = 0.0d0

  !> @brief Time step in seconds (set from clock at Realize time).
  integer, save :: g_time_step_secs = 3600

  !> @brief Module-level grid dimensions
  integer, save :: g_nx = 0, g_ny = 0, g_nz = 0

  !> @brief Module-level mesh for conservative regridding (created from grid coords)
  type(ESMF_Mesh), save :: g_mesh
  logical, save :: g_mesh_created = .false.

  !> @brief Dummy data buffer for standalone core testing without TIDE
  real(c_double), allocatable, save, target :: g_dummy_data_buffer(:,:)
  logical, save :: g_dummy_initialized = .false.


  ! C interface to CECE core
  interface
    subroutine cece_set_config_file_path(config_path, path_len) bind(C)
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
    end subroutine
    subroutine cece_core_advertise(importState, exportState, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: importState, exportState
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_realize(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_initialize_p1(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), intent(out) :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_initialize_p2(data_ptr, nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: nx, ny, nz
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_species_count(data_ptr, count, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_species_name(data_ptr, index, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: index
      character(kind=c_char), intent(out) :: name(*)
      integer(c_int), intent(out) :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_grid_config(data_ptr, nx, ny, lon_min, lon_max, lat_min, lat_max, rc) bind(C)
      import :: c_ptr, c_int, c_double
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: nx, ny
      real(c_double), intent(out) :: lon_min, lon_max, lat_min, lat_max
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_timing_config(data_ptr, start_time, end_time, timestep_seconds, max_len, rc) bind(C)
      import :: c_ptr, c_int, c_char
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(out) :: start_time(*), end_time(*)
      integer(c_int), intent(out) :: timestep_seconds
      integer(c_int), value :: max_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_external_field_count(data_ptr, count, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_external_field_name(data_ptr, index, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: index
      character(kind=c_char), intent(out) :: name(*)
      integer(c_int), intent(out) :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_stream_field_count(data_ptr, count, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_stream_field_name(data_ptr, index, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: index
      character(kind=c_char), intent(out) :: name(*)
      integer(c_int), intent(out) :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_bind_fields(data_ptr, field_ptrs, num_fields, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      type(c_ptr), intent(in) :: field_ptrs(*)
      integer(c_int), intent(in) :: num_fields
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_writer_initialize(data_ptr, nx, ny, nz, start_time, start_time_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_writer_initialize_with_coords(data_ptr, nx, ny, nz, lon_coords, lat_coords, &
                                                      start_time, start_time_len, rc) bind(C)
      import :: c_ptr, c_char, c_int, c_double
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      real(c_double), intent(in) :: lon_coords(nx), lat_coords(ny)
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_get_ingestor_streams_path(data_ptr, streams_path, path_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(out) :: streams_path(*)
      integer(c_int), intent(out) :: path_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_run(data_ptr, hour, day_of_week, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: hour
      integer(c_int), value :: day_of_week
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_finalize(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_set_export_field(data_ptr, name, name_len, field_data, nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len
      type(c_ptr), value :: field_data
      integer(c_int), value :: nx, ny, nz
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_core_write_step(data_ptr, time_seconds, step_index, rc) bind(C)
      import :: c_ptr, c_double, c_int
      type(c_ptr), value :: data_ptr
      real(c_double), value :: time_seconds
      integer(c_int), value :: step_index
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine cece_ingestor_set_field(data_ptr, field_name, name_len, field_data, n_lev, n_elem, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: field_name(*)
      integer(c_int), value :: name_len
      type(c_ptr), value :: field_data
      integer(c_int), value :: n_lev
      integer(c_int), value :: n_elem
      integer(c_int), intent(out) :: rc
    end subroutine
    ! Meteorology registry C API interfaces
    function cece_get_met_registry_count() bind(C) result(n)
      import :: c_size_t
      integer(c_size_t) :: n
    end function cece_get_met_registry_count
    function cece_get_met_registry_internal_name(idx) bind(C) result(name)
      import :: c_size_t, c_ptr
      integer(c_size_t), value :: idx
      type(c_ptr) :: name
    end function cece_get_met_registry_internal_name
    function cece_get_met_registry_alias_count(idx) bind(C) result(n)
      import :: c_size_t
      integer(c_size_t), value :: idx
      integer(c_size_t) :: n
    end function cece_get_met_registry_alias_count
    function cece_get_met_registry_alias(idx, alias_idx) bind(C) result(alias)
      import :: c_size_t, c_ptr
      integer(c_size_t), value :: idx
      integer(c_size_t), value :: alias_idx
      type(c_ptr) :: alias
    end function cece_get_met_registry_alias
  end interface

contains

  !> @brief Set the configuration file path for CECE initialization.
  !> This should be called before ESMF_GridCompSetServices to ensure the
  !> correct config file is used during initialization phases.
  subroutine CECE_SetConfigPath(config_path, rc)
    character(len=*), intent(in) :: config_path
    integer, intent(out) :: rc

    if (len_trim(config_path) > 0 .and. len_trim(config_path) <= 512) then
      g_config_file_path = trim(config_path)
      rc = ESMF_SUCCESS
    else
      write(*,'(A)') "ERROR: [CECE] Invalid config path length"
      rc = ESMF_FAILURE
    end if
  end subroutine CECE_SetConfigPath

  subroutine CECE_SetServices(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    type(ESMF_State) :: dummy_import, dummy_export
    type(ESMF_Clock) :: dummy_clock

    write(*,'(A)') "INFO: [CECE] CECE_SetServices entered"

    ! 1. Inherit NUOPC_Model base phases
    write(*,'(A)') "INFO: [CECE] Calling NUOPC_CompDerive..."
    call NUOPC_CompDerive(comp, modelSS, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompDerive failed rc=", rc
      return
    end if
    write(*,'(A)') "INFO: [CECE] NUOPC_CompDerive succeeded"

    ! 2. Call the phase map routine directly to filter and replace init routines
    write(*,'(A)') "INFO: [CECE] Calling CECE_InitPhaseMap..."
    call CECE_InitPhaseMap(comp, dummy_import, dummy_export, dummy_clock, rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: CECE_InitPhaseMap failed rc=", rc
      return
    end if
    write(*,'(A)') "INFO: [CECE] CECE_InitPhaseMap succeeded"

    ! 3. Specialize Run (Advance)
    write(*,'(A)') "INFO: [CECE] Specializing Run phase..."
    call NUOPC_CompSpecialize(comp, specLabel=model_label_Advance, &
      specRoutine=CECE_Run, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompSpecialize(Advance) failed rc=", rc
      return
    end if
    write(*,'(A)') "INFO: [CECE] Run phase specialized"

    ! 4. Specialize Finalize
    write(*,'(A)') "INFO: [CECE] Specializing Finalize phase..."
    call NUOPC_CompSpecialize(comp, specLabel=model_label_Finalize, &
      specRoutine=CECE_Finalize, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompSpecialize(Finalize) failed rc=", rc
      return
    end if
    write(*,'(A)') "INFO: [CECE] Finalize phase specialized"

    write(*,'(A)') "INFO: [CECE] CECE_SetServices complete"
    rc = ESMF_SUCCESS
  end subroutine CECE_SetServices

  !> @brief Phase 0: filter the NUOPC initialize phase map to IPDv01 and
  !> replace the base NUOPC_ModelBase IPDv01p1/p3 entry points with our
  !> implementations. Called during ESMF_GridCompSetServices before any init phases run.
  !> Identical pattern to TIDE dshr_model_initphase.
  subroutine CECE_InitPhaseMap(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    write(*,'(A)') "INFO: [CECE] CECE_InitPhaseMap entered"
    rc = ESMF_SUCCESS

    ! Filter to IPDv01 — removes all other IPD version entries
    write(*,'(A)') "INFO: [CECE] Calling NUOPC_CompFilterPhaseMap..."
    call NUOPC_CompFilterPhaseMap(comp, ESMF_METHOD_INITIALIZE, &
      acceptStringList=(/"IPDv01p"/), rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_CompFilterPhaseMap failed rc=", rc
      return
    end if
    write(*,'(A)') "INFO: [CECE] NUOPC_CompFilterPhaseMap succeeded"

    ! Now register our implementations for phases 1 and 2
    ! These replace whatever was there before (if anything)
    write(*,'(A)') "INFO: [CECE] Setting entry point for phase 1..."
    call ESMF_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, &
      userRoutine=CECE_InitializeAdvertise, phase=1, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: SetEntryPoint(phase=1) failed rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [CECE] Setting entry point for phase 2..."
    call ESMF_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, &
      userRoutine=CECE_InitializeRealize, phase=2, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: SetEntryPoint(phase=2) failed rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [CECE] CECE_InitPhaseMap complete"
    rc = ESMF_SUCCESS
  end subroutine CECE_InitPhaseMap

  !> @brief IPDv01p1: Advertise fields and perform core initialization.
  !> Runs Kokkos init, YAML config parse, PhysicsFactory, StackingEngine setup,
  !> and advertises all export fields. Equivalent to DATM's InitializeAdvertise.
  subroutine CECE_InitializeAdvertise(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(c_ptr)    :: new_data_ptr
    character(len=512) :: config_path
    integer :: config_len

    write(*,'(A)') "INFO: [CECE] CECE_InitializeAdvertise entered - PHASE 1 STARTING"

    write(*,'(A)') "INFO: [CECE] InitializeAdvertise (IPDv01p1) entered"

    ! Get the config file path from the component's internal state if available
    ! For now, use the module-level default or environment variable
    config_path = g_config_file_path
    config_len = len_trim(config_path)

    ! Set the config path in C++ before initialization
    call cece_set_config_file_path(config_path(1:config_len)//c_null_char, int(config_len, c_int))

    ! --- Core init: Kokkos, YAML config, PhysicsFactory, StackingEngine ---
    call cece_core_initialize_p1(new_data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: cece_core_initialize_p1 failed rc=", rc
      return
    end if
    g_cece_data_ptr = new_data_ptr
    write(*,'(A)') "INFO: [CECE] Core initialized, data pointer stored"

    ! --- Advertise fields ---
    ! Call cece_core_advertise to log what fields will be created
    ! In NUOPC, the Advertise phase is informational - fields are created in Realize
    call cece_core_advertise(transfer(importState, c_null_ptr), &
                             transfer(exportState, c_null_ptr), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: cece_core_advertise failed rc=", rc
      return
    end if

    ! --- Dynamically advertise ImportState fields from meteorology registry ---
    call advertise_met_registry_fields(importState, rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: Failed to advertise meteorology registry fields rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [CECE] InitializeAdvertise complete"
    rc = ESMF_SUCCESS
  end subroutine CECE_InitializeAdvertise

  !> @brief IPDv01p3: Realize fields and bind TIDE data streams.
  !> Creates and allocates ESMF fields, then runs cece_core_initialize_p2
  !> for TIDE initialization and field binding.
  !> Equivalent to DATM's InitializeRealize.
  subroutine CECE_InitializeRealize(comp, importState, exportState, clock, rc)
    type(ESMF_GridComp)  :: comp
    type(ESMF_State)     :: importState, exportState
    type(ESMF_Clock)     :: clock
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    type(ESMF_Grid) :: grid  ! Keep grid for ESMF field creation (3D compatibility)
    type(ESMF_Mesh) :: mesh  ! Use mesh for TIDE conservative regridding
    integer :: nx, ny, nz
    real(ESMF_KIND_R8) :: lon_min, lon_max, lat_min, lat_max  ! Grid domain bounds
    integer, allocatable :: minIndex(:), maxIndex(:)
    integer :: num_species, num_fields, i
    character(len=256) :: species_name, field_name
    integer(c_int) :: species_name_len, field_name_len
    character(len=512) :: streams_path
    integer(c_int) :: streams_path_len
    type(ESMF_Field) :: field
    real(ESMF_KIND_R8), pointer :: fptr(:,:,:)
    type(c_ptr), allocatable :: field_ptrs(:)
    character(len=512) :: error_msg
    logical :: use_ingestor

    write(*,'(A)') "INFO: [CECE] InitializeRealize (IPDv01p3) entered"

    ! Reset step counter for this run
    g_step_count = 0

    ! Try to get mesh from component first (coupled mode)
    call ESMF_GridCompGet(comp, mesh=mesh, rc=rc)
    if (rc == ESMF_SUCCESS) then
      write(*,'(A)') "INFO: [CECE] Mesh retrieved from component (coupled mode)"
      ! Extract nx/ny from inherited mesh for TIDE compatibility
      ! TODO: Implement mesh analysis to get equivalent grid dimensions

      ! For now, read grid configuration from YAML to get dimensions and bounds
      call cece_core_get_grid_config(g_cece_data_ptr, nx, ny, lon_min, lon_max, lat_min, lat_max, c_rc)
      if (c_rc /= 0) then
        write(*,'(A,I0)') "WARNING: [CECE] Failed to get grid config in coupled mode, using defaults: rc=", c_rc
        nx = 4; ny = 4
        lon_min = -135._ESMF_KIND_R8; lon_max = 135._ESMF_KIND_R8
        lat_min = -67.5_ESMF_KIND_R8; lat_max = 67.5_ESMF_KIND_R8
      end if

      ! Create matching grid for ESMF field creation (3D compatibility)
      grid = ESMF_GridCreateNoPeriDimUfrm(maxIndex=(/nx, ny/), &
        minCornerCoord=(/lon_min, lat_min/), &
        maxCornerCoord=(/lon_max, lat_max/), &
        coordSys=ESMF_COORDSYS_SPH_DEG, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] Failed to create matching grid: rc=", rc
        return
      end if
      write(*,'(A)') "INFO: [CECE] Created matching grid for field creation"
    else
      write(*,'(A,I0)') "INFO: [CECE] No mesh provided by driver (rc=", rc, ") - creating component mesh and grid (standalone mode)"

      ! Read grid configuration from CECE config file
      call cece_core_get_grid_config(g_cece_data_ptr, nx, ny, lon_min, lon_max, lat_min, lat_max, c_rc)
      if (c_rc /= 0) then
        write(*,'(A,I0)') "WARNING: [CECE] Failed to get grid config, using defaults: rc=", c_rc
        nx = 4; ny = 4
        lon_min = -135._ESMF_KIND_R8; lon_max = 135._ESMF_KIND_R8
        lat_min = -67.5_ESMF_KIND_R8; lat_max = 67.5_ESMF_KIND_R8
      end if

      write(*,'(A,I0,A,I0)') "INFO: [CECE] Using grid configuration: nx=", nx, " ny=", ny
      write(*,'(A,F8.1,A,F8.1)') "INFO: [CECE] Domain: lon=", lon_min, " to ", lon_max
      write(*,'(A,F8.1,A,F8.1)') "INFO: [CECE] Domain: lat=", lat_min, " to ", lat_max

      ! Create mesh for TIDE conservative regridding
      call CreateMeshFromConfig(nx, ny, lon_min, lon_max, lat_min, lat_max, mesh, rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] Failed to create component mesh: rc=", rc
        return
      end if

      ! Create matching grid for ESMF field creation (3D compatibility)
      grid = ESMF_GridCreateNoPeriDimUfrm(maxIndex=(/nx, ny/), &
        minCornerCoord=(/lon_min, lat_min/), &
        maxCornerCoord=(/lon_max, lat_max/), &
        coordSys=ESMF_COORDSYS_SPH_DEG, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] Failed to create component grid: rc=", rc
        return
      end if

      ! Set both mesh and grid on the component
      call ESMF_GridCompSet(comp, mesh=mesh, grid=grid, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] Failed to set created mesh and grid on component: rc=", rc
        return
      end if

      ! Store the created mesh globally so we can destroy it during finalization
      g_mesh = mesh
      g_mesh_created = .true.
      write(*,'(A)') "INFO: [CECE] Component mesh and grid created successfully from YAML configuration"
    end if

    ! Get nx/ny dimensions (already set above based on mesh creation or inheritance)
    ! nx and ny are already properly set from mesh creation or analysis

    ! nz: get from config via C++ (number of vertical levels)
    nz = 10  ! default

    ! --- Check if ingestor streams are configured early ---
    write(*,'(A)') "INFO: [CECE] Checking for ingestor streams configuration"
    call cece_core_get_ingestor_streams_path(g_cece_data_ptr, streams_path, &
                                             streams_path_len, c_rc)
    use_ingestor = (c_rc == 0 .and. streams_path_len > 0)

    if (use_ingestor) then
      write(*,'(A)') "INFO: [CECE] Ingestor streams configured - will use grid-based regridding"
      write(*,'(A,A)') "INFO: [CECE] Streams path: ", trim(streams_path(1:int(streams_path_len)))
    else
      write(*,'(A)') "INFO: [CECE] No ingestor streams configured - will create grid-based fields"
    end if

    allocate(minIndex(3), maxIndex(3))
    minIndex = [1, 1, 1]
    maxIndex = [nx, ny, nz]

    write(*,'(A,I0,A,I0,A,I0)') "INFO: [CECE] Grid dimensions: nx=", nx, " ny=", ny, " nz=", nz

    ! Store globals
    g_nx = nx
    g_ny = ny
    g_nz = nz

    ! Call cece_core_realize (validates config)
    call cece_core_realize(g_cece_data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "WARNING: [CECE] cece_core_realize returned non-success: rc=", rc
      rc = ESMF_SUCCESS
    end if

    ! --- Phase 2: TIDE init and field binding ---
    ! Pass grid dimensions to C++ (extracted from ESMF grid)
    call cece_core_initialize_p2(g_cece_data_ptr, int(nx, c_int), int(ny, c_int), &
                                 int(nz, c_int), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [CECE] cece_core_initialize_p2 failed: rc=", rc
      deallocate(minIndex, maxIndex)
      return
    end if

    ! --- Initialize standalone writer if output is configured ---
    ! Get current time from clock for start_time_iso8601
    block
      type(ESMF_Time) :: currTime
      type(ESMF_TimeInterval) :: timeStep
      integer :: yy, mm, dd, h, m, s
      integer :: dt_secs
      character(len=32) :: start_time_str

      call ESMF_ClockGet(clock, currTime=currTime, timeStep=timeStep, rc=rc)
      if (rc == ESMF_SUCCESS) then
        call ESMF_TimeGet(currTime, yy=yy, mm=mm, dd=dd, h=h, m=m, s=s, rc=rc)
        if (rc == ESMF_SUCCESS) then
          write(start_time_str, '(I4.4,A,I2.2,A,I2.2,A,I2.2,A,I2.2,A,I2.2)') &
            yy, '-', mm, '-', dd, 'T', h, ':', m, ':', s
          ! Store start time as seconds (approximate, for elapsed time tracking)
          g_start_time_seconds = real(yy*365*24*3600 + mm*30*24*3600 + dd*24*3600 + &
                                      h*3600 + m*60 + s, c_double)

          ! Extract coordinates from ESMF grid and call enhanced writer initialization
          block
            real(ESMF_KIND_R8), pointer :: grid_lon(:,:), grid_lat(:,:)
            real(c_double), allocatable :: lon_coords(:), lat_coords(:)
            integer :: i

            ! Get coordinates from ESMF grid (2D arrays)
            call ESMF_GridGetCoord(grid, coordDim=1, localDE=0, staggerloc=ESMF_STAGGERLOC_CENTER, &
                                  farrayptr=grid_lon, rc=rc)
            if (rc /= ESMF_SUCCESS) then
              write(*,'(A,I0)') "WARNING: [CECE] Failed to get longitude coordinates: rc=", rc
              ! Fall back to legacy initialization
              call cece_core_writer_initialize(g_cece_data_ptr, int(nx, c_int), int(ny, c_int), &
                                              int(nz, c_int), start_time_str, int(len_trim(start_time_str), c_int), c_rc)
            else
              call ESMF_GridGetCoord(grid, coordDim=2, localDE=0, staggerloc=ESMF_STAGGERLOC_CENTER, &
                                    farrayptr=grid_lat, rc=rc)
              if (rc /= ESMF_SUCCESS) then
                write(*,'(A,I0)') "WARNING: [CECE] Failed to get latitude coordinates: rc=", rc
                ! Fall back to legacy initialization
                call cece_core_writer_initialize(g_cece_data_ptr, int(nx, c_int), int(ny, c_int), &
                                                int(nz, c_int), start_time_str, int(len_trim(start_time_str), c_int), c_rc)
              else
                ! Extract 1D coordinate arrays from 2D ESMF grid coordinates
                allocate(lon_coords(nx), lat_coords(ny))
                do i = 1, nx
                  lon_coords(i) = real(grid_lon(i,1), c_double)  ! First row, all columns
                end do
                do i = 1, ny
                  lat_coords(i) = real(grid_lat(1,i), c_double)  ! First column, all rows
                end do

                write(*,'(A,2F10.3)') "INFO: [CECE] Grid longitude range: ", lon_coords(1), lon_coords(nx)
                write(*,'(A,2F10.3)') "INFO: [CECE] Grid latitude range: ", lat_coords(1), lat_coords(ny)

                ! Call enhanced writer initialization with coordinates
                call cece_core_writer_initialize_with_coords(g_cece_data_ptr, int(nx, c_int), int(ny, c_int), &
                                                           int(nz, c_int), lon_coords, lat_coords, &
                                                           start_time_str, int(len_trim(start_time_str), c_int), c_rc)
                deallocate(lon_coords, lat_coords)
              end if
            end if
          end block

          rc = int(c_rc)
          if (rc /= ESMF_SUCCESS) then
            write(*,'(A,I0)') "WARNING: [CECE] Writer initialization failed: rc=", rc
            rc = ESMF_SUCCESS  ! Non-fatal
          end if
        end if
        ! Capture time step size for elapsed time computation in Run
        call ESMF_TimeIntervalGet(timeStep, s=dt_secs, rc=rc)
        if (rc == ESMF_SUCCESS) then
          g_time_step_secs = dt_secs
          write(*,'(A,I0)') "DEBUG: [CECE_CAP] Clock timestep extracted: ", dt_secs, " seconds"
        else
          g_time_step_secs = 3600
          write(*,'(A)') "DEBUG: [CECE_CAP] Failed to get timestep, using default 3600s"
          rc = ESMF_SUCCESS
        end if
      end if
    end block

    ! --- Create ESMF fields for each species ---
    ! Get the number of species from C++
    call cece_core_get_species_count(g_cece_data_ptr, num_species, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [CECE] cece_core_get_species_count failed: rc=", rc
      deallocate(minIndex, maxIndex)
      return
    end if

    if (num_species <= 0) then
      write(*,'(A,I0)') "ERROR: [CECE] Invalid species count from C++: ", num_species
      deallocate(minIndex, maxIndex)
      rc = ESMF_FAILURE
      return
    end if

    write(*,'(A,I0,A)') "INFO: [CECE] Creating ESMF fields for ", num_species, " species"

    ! Allocate array to store field data pointers
    allocate(field_ptrs(num_species))

    ! Create a field for each species
    do i = 1, num_species
      ! Get the actual species name from C++
      call cece_core_get_species_name(g_cece_data_ptr, int(i-1, c_int), species_name, &
                                      species_name_len, c_rc)
      if (c_rc /= 0 .or. species_name_len <= 0) then
        ! Fall back to generic name if retrieval fails
        write(species_name, '(A,I0)') "species_", i
        species_name_len = len_trim(species_name)
      end if

      ! Create 3D ESMF field on grid.
      ! Grid fields: gridded dims = (nx, ny) (rank-2); ungridded = vertical levels.
      field = ESMF_FieldCreate(grid, ESMF_TYPEKIND_R8, &
                               name=species_name(1:int(species_name_len)), &
                               ungriddedLBound=[1], ungriddedUBound=[nz], rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,A,A,I0)') "ERROR: [CECE] ESMF_FieldCreate failed for species ", i, &
                                   " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      ! Extract the raw data pointer.
      ! Grid fields are stored as (nx, ny, nz) — use rank-3 farrayPtr.
      call ESMF_FieldGet(field, localDe=0, farrayPtr=fptr, rc=rc)
      if (rc /= ESMF_SUCCESS .or. .not. associated(fptr)) then
        write(*,'(A,I0,A,A,A,I0)') &
          "ERROR: [CECE] ESMF_FieldGet(grid farrayPtr) failed for species ", i, &
          " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if
      field_ptrs(i) = c_loc(fptr(1,1,1))

      ! Register field pointer in C++ export state FieldMap
      call cece_core_set_export_field(g_cece_data_ptr, &
          species_name(1:int(species_name_len))//c_null_char, &
          int(species_name_len, c_int), &
          field_ptrs(i), &
          int(nx, c_int), int(ny, c_int), int(nz, c_int), c_rc)
      if (c_rc /= 0) then
        write(*,'(A,A)') "WARNING: [CECE] cece_core_set_export_field failed for: ", &
                         trim(species_name(1:int(species_name_len)))
      end if

      ! Add field to export state using ESMF_StateAdd with fieldList keyword
      call ESMF_StateAdd(exportState, fieldList=[field], rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,A,A,I0)') "ERROR: [CECE] ESMF_StateAdd failed for species ", i, &
                                   " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      write(*,'(A,I0,A,A,A)') "INFO: [CECE] Successfully created field for species ", i, &
                              ": ", trim(species_name(1:int(species_name_len))), &
                              " with data pointer"
    end do

    ! Pass field pointers to C++ for storage in internal_data
    call cece_core_bind_fields(g_cece_data_ptr, field_ptrs, int(num_species, c_int), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [CECE] cece_core_bind_fields failed: rc=", rc
      deallocate(minIndex, maxIndex, field_ptrs)
      return
    end if

    ! --- Phase 4: Initialize ingestor if configured ---
    if (use_ingestor .and. .not. g_tide_initialized) then
      write(*,'(A)') "INFO: [CECE] Initializing TIDE ingestor..."

      ! Initialize TIDE with YAML configuration file path (not content)
      ! TIDE expects a filename and will read the file itself
      call tide_init(g_tide, trim(streams_path(1:int(streams_path_len))), mesh, clock, rc)

      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] TIDE initialization failed rc=", rc
        rc = ESMF_FAILURE
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      g_tide_initialized = .true.
      write(*,'(A)') "INFO: [CECE] TIDE initialized successfully"
    else
      if (use_ingestor) then
        write(*,'(A)') "INFO: [CECE] TIDE already initialized (idempotent)"
      else
        write(*,'(A)') "INFO: [CECE] No ingestor streams configured - continuing without ingestor"
      end if
    end if

    deallocate(minIndex, maxIndex, field_ptrs)
    write(*,'(A)') "INFO: [CECE] InitializeRealize completed successfully"
    rc = ESMF_SUCCESS
  end subroutine CECE_InitializeRealize

  !> @brief Run phase: advance TIDE and execute CECE physics/stacking.
  !>
  !> Extracts hour-of-day and day-of-week from the ESMF clock in Fortran
  !> (where ESMF derived types are safe) and passes plain integers to the
  !> ESMF-free C++ core.
  subroutine CECE_Run(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc, c_step_rc
    real(c_double) :: time_seconds
    integer :: hour, day_of_week

    integer :: tide_rc, num_fields, i
    type(ESMF_Clock) :: run_clock
    real(c_double), pointer :: ptr(:,:)
    character(len=256) :: field_name
    integer(c_int) :: field_name_len
    type(ESMF_State) :: importState, exportState
    type(ESMF_Clock) :: clock

    rc = ESMF_SUCCESS

    write(*,'(A)') "INFO: [CECE] CECE_Run entered"

    ! DEBUG: Check clock state at start of CECE_Run to track stop time corruption
    call ESMF_GridCompGet(comp, clock=clock, rc=rc)
    if (rc == ESMF_SUCCESS) then
      block
        type(ESMF_Time) :: DEBUG_currTime, DEBUG_stopTime
        character(len=32) :: DEBUG_curr_str, DEBUG_stop_str
        call ESMF_ClockGet(clock, currTime=DEBUG_currTime, stopTime=DEBUG_stopTime, rc=rc)
        if (rc == ESMF_SUCCESS) then
          call ESMF_TimeGet(DEBUG_currTime, timeStringISOFrac=DEBUG_curr_str, rc=rc)
          call ESMF_TimeGet(DEBUG_stopTime, timeStringISOFrac=DEBUG_stop_str, rc=rc)
          if (rc == ESMF_SUCCESS) then
            write(*,'(A,A)') "DEBUG: [CECE_CAP] Run entry - current=", trim(DEBUG_curr_str)
            write(*,'(A,A)') "DEBUG: [CECE_CAP] Run entry - stop=", trim(DEBUG_stop_str)
          end if
        end if
      end block
    end if

    ! Debug: Check what's available in the component
    block
      type(ESMF_Grid) :: comp_grid
      type(ESMF_Mesh) :: comp_mesh
      type(ESMF_Clock) :: comp_clock
      logical :: has_grid, has_mesh, has_clock

      call ESMF_GridCompGet(comp, grid=comp_grid, rc=rc)
      has_grid = (rc == ESMF_SUCCESS)
      write(*,'(A,L1,A,I0)') "DEBUG: [CECE] Component has grid: ", has_grid, " (rc=", rc, ")"

      rc = ESMF_SUCCESS  ! Reset for next check
      call ESMF_GridCompGet(comp, mesh=comp_mesh, rc=rc)
      has_mesh = (rc == ESMF_SUCCESS)
      write(*,'(A,L1,A,I0)') "DEBUG: [CECE] Component has mesh: ", has_mesh, " (rc=", rc, ")"

      rc = ESMF_SUCCESS  ! Reset for next check
      call ESMF_GridCompGet(comp, clock=comp_clock, rc=rc)
      has_clock = (rc == ESMF_SUCCESS)
      write(*,'(A,L1,A,I0)') "DEBUG: [CECE] Component has clock: ", has_clock, " (rc=", rc, ")"
    end block

    rc = ESMF_SUCCESS  ! Reset for main logic

    ! Check if CECE has been properly initialized
    if (.not. c_associated(g_cece_data_ptr)) then
      write(*,'(A)') "WARNING: [CECE] Run called but not initialized - performing emergency initialization"

      ! Get states and clock from the component for initialization
      call ESMF_GridCompGet(comp, importState=importState, exportState=exportState, clock=clock, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] Failed to get component states/clock for initialization: rc=", rc
        return
      end if

      ! Call initialization phases that should have been called by the framework
      write(*,'(A)') "INFO: [CECE] Calling CECE_InitializeAdvertise..."
      call CECE_InitializeAdvertise(comp, importState, exportState, clock, rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] Emergency InitializeAdvertise failed: rc=", rc
        return
      end if

      write(*,'(A)') "INFO: [CECE] Calling CECE_InitializeRealize..."
      call CECE_InitializeRealize(comp, importState, exportState, clock, rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [CECE] Emergency InitializeRealize failed: rc=", rc
        return
      end if

      write(*,'(A)') "INFO: [CECE] Emergency initialization complete"
    end if

    ! Now proceed with normal run logic
    if (.not. c_associated(g_cece_data_ptr)) then
      write(*,'(A)') "ERROR: [CECE] Run called but data pointer is still null after initialization"
      rc = ESMF_FAILURE
      return
    end if

    write(*,'(A)') "INFO: [CECE] CECE_Run proceeding with valid data pointer"

    ! Extract hour-of-day from the component clock (Fortran ESMF API is safe here)
    hour = 0
    day_of_week = 0
    block
      type(ESMF_Clock) :: run_clock_local
      type(ESMF_Time)  :: curr_time
      integer :: h, yy, mm, dd, local_rc
      call ESMF_GridCompGet(comp, clock=run_clock_local, rc=local_rc)
      if (local_rc == ESMF_SUCCESS) then
        call ESMF_ClockGet(run_clock_local, currTime=curr_time, rc=local_rc)
        if (local_rc == ESMF_SUCCESS) then
          call ESMF_TimeGet(curr_time, yy=yy, mm=mm, dd=dd, h=h, rc=local_rc)
          if (local_rc == ESMF_SUCCESS) hour = h
        end if
      end if
    end block

    ! --- TIDE Update ---
    if (g_tide_initialized) then
      call ESMF_GridCompGet(comp, clock=run_clock, rc=rc)
      if (rc == ESMF_SUCCESS) then

        ! Create a copy of the clock for TIDE to prevent it from corrupting our driver clock
        block
          type(ESMF_Clock) :: tide_clock
          type(ESMF_Time) :: currTime, stopTime
          type(ESMF_TimeInterval) :: timeStep

          ! Get parameters from original clock
          call ESMF_ClockGet(run_clock, currTime=currTime, stopTime=stopTime, &
                            timeStep=timeStep, rc=rc)
          if (rc == ESMF_SUCCESS) then
            ! Create independent clock for TIDE
            tide_clock = ESMF_ClockCreate(name="TIDE_Clock", &
              timeStep=timeStep, startTime=currTime, stopTime=stopTime, rc=rc)
            if (rc == ESMF_SUCCESS) then
              write(*,'(A)') "INFO: [CECE] Created independent TIDE clock to prevent corruption"

              ! Pass the copy to TIDE, not our precious driver clock
              call tide_advance(g_tide, tide_clock, tide_rc)
              if (tide_rc /= ESMF_SUCCESS) then
                 write(*,'(A,I0)') "ERROR: [CECE] TIDE advance failed rc=", tide_rc
              else
                 write(*,'(A)') "INFO: [CECE] TIDE advance succeeded with protected clock"
                 ! Critical: Force I/O synchronization after TIDE advance for large grids
                 if (g_nx * g_ny > 50000) then
                   write(*,'(A,I0)') "INFO: [CECE] Large grid detected, adding TIDE sync delay..."
                   call flush(6)  ! Force output buffer flush
                 end if

                 ! Transfer fields - iterate through stream fields for emission data
                 call cece_core_get_stream_field_count(g_cece_data_ptr, num_fields, c_rc)
                 if (c_rc == 0) then
                   do i = 0, num_fields-1
                     call cece_core_get_stream_field_name(g_cece_data_ptr, int(i, c_int), field_name, &
                                                          field_name_len, c_rc)
                     if (c_rc == 0 .and. field_name_len > 0) then
                       call tide_get_ptr(g_tide, field_name(1:int(field_name_len)), ptr, tide_rc)
                       if (tide_rc == 0 .and. associated(ptr)) then
                         write(*,'(A,A,A,I0,A,I0,A,I0)') "DEBUG: Field ", &
                              trim(field_name(1:int(field_name_len))), &
                              " ptr dimensions: ", size(ptr,1), " x ", size(ptr,2), &
                              " (total=", size(ptr), ")"
                         call cece_ingestor_set_field(g_cece_data_ptr, &
                              field_name(1:int(field_name_len))//c_null_char, &
                              field_name_len, c_loc(ptr(1,1)), &
                              int(size(ptr,1), c_int), int(size(ptr,2), c_int), c_rc)
                         if (c_rc /= 0) then
                           write(*,'(A,A)') "WARNING: [CECE] cece_ingestor_set_field failed for: ", &
                                             trim(field_name(1:int(field_name_len)))
                         end if
                       else
                         write(*,'(A,A,A,I0)') "WARNING: [CECE] tide_get_ptr failed for field ", &
                              trim(field_name(1:int(field_name_len))), " rc=", tide_rc
                       end if
                     end if
                   end do
                 end if
              end if

              ! Clean up TIDE clock copy - do this regardless of success/failure
              call ESMF_ClockDestroy(tide_clock, rc=rc)
              if (rc /= ESMF_SUCCESS) then
                write(*,'(A,I0)') "WARNING: [CECE] Failed to destroy TIDE clock copy rc=", rc
              end if
            else
              write(*,'(A,I0)') "ERROR: [CECE] Failed to create TIDE clock copy rc=", rc
              ! Fall back to original method if copy fails (risky but necessary)
              call tide_advance(g_tide, run_clock, tide_rc)
              if (tide_rc /= ESMF_SUCCESS) then
                 write(*,'(A,I0)') "ERROR: [CECE] TIDE advance (fallback) failed rc=", tide_rc
              end if
            end if
          else
            write(*,'(A,I0)') "ERROR: [CECE] Failed to get clock parameters for TIDE copy rc=", rc
            ! Fall back to original method
            call tide_advance(g_tide, run_clock, tide_rc)
            if (tide_rc /= ESMF_SUCCESS) then
               write(*,'(A,I0)') "ERROR: [CECE] TIDE advance (fallback) failed rc=", tide_rc
            end if
          end if
        end block
      end if
    endif

    call cece_core_run(g_cece_data_ptr, int(hour, c_int), int(day_of_week, c_int), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) return

    ! DEBUG: Check clock after cece_core_run
    block
      type(ESMF_Time) :: DEBUG_currTime, DEBUG_stopTime
      character(len=32) :: DEBUG_curr_str, DEBUG_stop_str
      call ESMF_GridCompGet(comp, clock=run_clock, rc=rc)
      if (rc == ESMF_SUCCESS) then
        call ESMF_ClockGet(run_clock, currTime=DEBUG_currTime, stopTime=DEBUG_stopTime, rc=rc)
        if (rc == ESMF_SUCCESS) then
          call ESMF_TimeGet(DEBUG_currTime, timeStringISOFrac=DEBUG_curr_str, rc=rc)
          call ESMF_TimeGet(DEBUG_stopTime, timeStringISOFrac=DEBUG_stop_str, rc=rc)
          if (rc == ESMF_SUCCESS) then
            write(*,'(A,A)') "DEBUG: [CECE_CAP] After cece_core_run - current=", trim(DEBUG_curr_str)
            write(*,'(A,A)') "DEBUG: [CECE_CAP] After cece_core_run - stop=", trim(DEBUG_stop_str)
          end if
        end if
      end if
    end block

    ! Write output step
    time_seconds = real(g_step_count * g_time_step_secs, c_double)
    call cece_core_write_step(g_cece_data_ptr, time_seconds, int(g_step_count, c_int), c_step_rc)
    if (c_step_rc /= 0) then
      write(*,'(A)') "WARNING: [CECE] cece_core_write_step failed"
    end if

    ! DEBUG: Check clock after cece_core_write_step
    block
      type(ESMF_Time) :: DEBUG_currTime, DEBUG_stopTime
      character(len=32) :: DEBUG_curr_str, DEBUG_stop_str
      call ESMF_GridCompGet(comp, clock=run_clock, rc=rc)
      if (rc == ESMF_SUCCESS) then
        call ESMF_ClockGet(run_clock, currTime=DEBUG_currTime, stopTime=DEBUG_stopTime, rc=rc)
        if (rc == ESMF_SUCCESS) then
          call ESMF_TimeGet(DEBUG_currTime, timeStringISOFrac=DEBUG_curr_str, rc=rc)
          call ESMF_TimeGet(DEBUG_stopTime, timeStringISOFrac=DEBUG_stop_str, rc=rc)
          if (rc == ESMF_SUCCESS) then
            write(*,'(A,A)') "DEBUG: [CECE_CAP] After cece_core_write_step - current=", trim(DEBUG_curr_str)
            write(*,'(A,A)') "DEBUG: [CECE_CAP] After cece_core_write_step - stop=", trim(DEBUG_stop_str)
          end if
        end if
      end if
    end block
    g_step_count = g_step_count + 1

    ! Critical: Final synchronization for large grids before returning
    if (g_nx * g_ny > 50000) then
      write(*,'(A,I0)') "INFO: [CECE] Large grid final sync (", g_nx * g_ny, " points)..."
      call flush(6)  ! Force all I/O completion
    end if

    ! DEBUG: Check clock state at end of CECE_Run to see if we corrupted it
    block
      type(ESMF_Time) :: DEBUG_currTime, DEBUG_stopTime
      character(len=32) :: DEBUG_curr_str, DEBUG_stop_str
      call ESMF_GridCompGet(comp, clock=clock, rc=rc)
      if (rc == ESMF_SUCCESS) then
        call ESMF_ClockGet(clock, currTime=DEBUG_currTime, stopTime=DEBUG_stopTime, rc=rc)
        if (rc == ESMF_SUCCESS) then
          call ESMF_TimeGet(DEBUG_currTime, timeStringISOFrac=DEBUG_curr_str, rc=rc)
          call ESMF_TimeGet(DEBUG_stopTime, timeStringISOFrac=DEBUG_stop_str, rc=rc)
          if (rc == ESMF_SUCCESS) then
            write(*,'(A,A)') "DEBUG: [CECE_CAP] Run exit - current=", trim(DEBUG_curr_str)
            write(*,'(A,A)') "DEBUG: [CECE_CAP] Run exit - stop=", trim(DEBUG_stop_str)
          end if
        end if
      end if
    end block

    write(*,'(A)') "INFO: [CECE] CECE_Run returning..."
    flush(6)

  end subroutine CECE_Run

  !> @brief Finalize phase: release all CECE resources.
  subroutine CECE_Finalize(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    integer :: tide_rc

    write(*,'(A)') "INFO: CECE Finalize - beginning cleanup"

    if (.not. c_associated(g_cece_data_ptr)) then
      write(*,'(A)') "WARNING: [CECE] Finalize called but data pointer is null"
      rc = ESMF_SUCCESS
      return
    end if

    if (g_tide_initialized) then
      write(*,'(A)') "INFO: Finalizing TIDE..."
      call tide_finalize(g_tide, tide_rc)
      if (tide_rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "WARNING: [CECE] TIDE finalize returned non-success rc=", tide_rc
      else
        write(*,'(A)') "INFO: TIDE finalized successfully"
      end if
      g_tide_initialized = .false.
    end if

    ! NOTE: Don't destroy the mesh here - let ESMF framework handle cleanup
    ! Destroying it manually can interfere with NUOPC/ESMF finalization order
    if (g_mesh_created) then
      write(*,'(A)') "INFO: [CECE] Mesh will be cleaned up by ESMF framework"
      g_mesh_created = .false.
    end if

    write(*,'(A)') "INFO: Calling CECE core finalize..."
    call cece_core_finalize(g_cece_data_ptr, c_rc)
    g_cece_data_ptr = c_null_ptr
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: cece_core_finalize failed rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [CECE] Finalize complete"
    rc = ESMF_SUCCESS
  end subroutine CECE_Finalize

  !> @brief Create a mesh directly from configuration parameters for conservative regridding
  subroutine CreateMeshFromConfig(nx, ny, lon_min, lon_max, lat_min, lat_max, mesh, rc)
    integer, intent(in) :: nx, ny
    real(ESMF_KIND_R8), intent(in) :: lon_min, lon_max, lat_min, lat_max
    type(ESMF_Mesh), intent(out) :: mesh
    integer, intent(out) :: rc

    integer :: i, j, nodeIdx, elemIdx
    integer :: totalNodes, totalElems
    real(ESMF_KIND_R8), allocatable :: nodeCoords(:)
    integer, allocatable :: nodeIds(:), nodeOwners(:), elemIds(:), elemTypes(:), elemConn(:)
    integer :: nodesPerElem
    type(ESMF_VM) :: vm
    integer :: localPet, petCount

    rc = ESMF_SUCCESS

    ! Get MPI info
    call ESMF_VMGetCurrent(vm, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    call ESMF_VMGet(vm, localPet=localPet, petCount=petCount, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Create mesh
    mesh = ESMF_MeshCreate(parametricDim=2, spatialDim=2, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Only PET0 creates the global mesh structure for simplicity
    if (localPet == 0) then
      ! For conservative regridding, we need nodes at grid corners and elements at grid centers
      ! Grid cells become mesh elements (quads)
      totalNodes = (nx + 1) * (ny + 1)
      totalElems = nx * ny
      nodesPerElem = 4

      ! Allocate arrays
      allocate(nodeCoords(totalNodes * 2))  ! x,y for each node
      allocate(nodeIds(totalNodes))
      allocate(nodeOwners(totalNodes))
      allocate(elemIds(totalElems))
      allocate(elemTypes(totalElems))
      allocate(elemConn(totalElems * nodesPerElem))

      ! Create regular lat-lon mesh coordinates
      nodeIdx = 0
      do j = 0, ny
        do i = 0, nx
          nodeIdx = nodeIdx + 1
          nodeIds(nodeIdx) = nodeIdx
          nodeOwners(nodeIdx) = 0

          ! Create coordinates from config parameters
          nodeCoords(2*nodeIdx-1) = lon_min + ((lon_max - lon_min) * i) / nx
          nodeCoords(2*nodeIdx) = lat_min + ((lat_max - lat_min) * j) / ny
        end do
      end do

      ! Create elements (grid cells as quads)
      elemIdx = 0
      do j = 1, ny
        do i = 1, nx
          elemIdx = elemIdx + 1
          elemIds(elemIdx) = elemIdx
          elemTypes(elemIdx) = ESMF_MESHELEMTYPE_QUAD

          ! Connect nodes to form quadrilateral
          ! Node numbering: bottom-left, bottom-right, top-right, top-left
          elemConn(4*(elemIdx-1)+1) = (j-1)*(nx+1) + i      ! bottom-left
          elemConn(4*(elemIdx-1)+2) = (j-1)*(nx+1) + (i+1)  ! bottom-right
          elemConn(4*(elemIdx-1)+3) = j*(nx+1) + (i+1)      ! top-right
          elemConn(4*(elemIdx-1)+4) = j*(nx+1) + i          ! top-left
        end do
      end do

      ! Add nodes to mesh
      call ESMF_MeshAddNodes(mesh, nodeIds, nodeCoords, nodeOwners, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        deallocate(nodeCoords, nodeIds, nodeOwners, elemIds, elemTypes, elemConn)
        return
      end if

      ! Add elements to mesh
      call ESMF_MeshAddElements(mesh, elemIds, elemTypes, elemConn, rc=rc)

      ! Clean up
      deallocate(nodeCoords, nodeIds, nodeOwners, elemIds, elemTypes, elemConn)
    else
      ! Other PETs contribute empty arrays
      allocate(nodeCoords(0), nodeIds(0), nodeOwners(0))
      allocate(elemIds(0), elemTypes(0), elemConn(0))

      call ESMF_MeshAddNodes(mesh, nodeIds, nodeCoords, nodeOwners, rc=rc)
      if (rc == ESMF_SUCCESS) then
        call ESMF_MeshAddElements(mesh, elemIds, elemTypes, elemConn, rc=rc)
      end if

      deallocate(nodeCoords, nodeIds, nodeOwners, elemIds, elemTypes, elemConn)
    end if

  end subroutine CreateMeshFromConfig

  !> @brief Create a properly structured mesh from grid coordinates for conservative regridding
  subroutine CreateMeshFromGridCoords(grid, mesh, rc)
    type(ESMF_Grid), intent(in) :: grid
    type(ESMF_Mesh), intent(out) :: mesh
    integer, intent(out) :: rc

    integer :: nx, ny, i, j, nodeIdx, elemIdx
    real(ESMF_KIND_R8), pointer :: lonPtr(:,:), latPtr(:,:)
    integer :: localDE, elemlb(2), elemub(2), nodelb(2), nodeub(2)
    integer :: totalNodes, totalElems
    real(ESMF_KIND_R8), allocatable :: nodeCoords(:)
    integer, allocatable :: nodeIds(:), nodeOwners(:), elemIds(:), elemTypes(:), elemConn(:)
    integer :: nodesPerElem
    type(ESMF_VM) :: vm
    integer :: localPet, petCount

    rc = ESMF_SUCCESS

    ! Get MPI info
    call ESMF_VMGetCurrent(vm, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    call ESMF_VMGet(vm, localPet=localPet, petCount=petCount, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Get grid dimensions
    localDE = 0
    call ESMF_GridGet(grid, localDE=localDE, staggerloc=ESMF_STAGGERLOC_CENTER, &
                      computationalLBound=elemlb, computationalUBound=elemub, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    nx = elemub(1) - elemlb(1) + 1
    ny = elemub(2) - elemlb(2) + 1

    ! Create mesh
    mesh = ESMF_MeshCreate(parametricDim=2, spatialDim=2, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Only PET0 creates the global mesh structure for simplicity
    if (localPet == 0) then
      ! For conservative regridding, we need nodes at grid corners and elements at grid centers
      ! Grid cells become mesh elements (quads)
      totalNodes = (nx + 1) * (ny + 1)
      totalElems = nx * ny
      nodesPerElem = 4

      ! Allocate arrays
      allocate(nodeCoords(totalNodes * 2))  ! x,y for each node
      allocate(nodeIds(totalNodes))
      allocate(nodeOwners(totalNodes))
      allocate(elemIds(totalElems))
      allocate(elemTypes(totalElems))
      allocate(elemConn(totalElems * nodesPerElem))

      ! Create regular lat-lon grid coordinates
      ! This is a simplified approach - in production you'd extract actual grid coordinates
      nodeIdx = 0
      do j = 0, ny
        do i = 0, nx
          nodeIdx = nodeIdx + 1
          nodeIds(nodeIdx) = nodeIdx
          nodeOwners(nodeIdx) = 0

          ! Create regular grid: longitude from -180 to 180, latitude from -90 to 90
          nodeCoords(2*nodeIdx-1) = -180.0_ESMF_KIND_R8 + (360.0_ESMF_KIND_R8 * i) / nx
          nodeCoords(2*nodeIdx) = -90.0_ESMF_KIND_R8 + (180.0_ESMF_KIND_R8 * j) / ny
        end do
      end do

      ! Create elements (grid cells as quads)
      elemIdx = 0
      do j = 1, ny
        do i = 1, nx
          elemIdx = elemIdx + 1
          elemIds(elemIdx) = elemIdx
          elemTypes(elemIdx) = ESMF_MESHELEMTYPE_QUAD

          ! Connect nodes to form quadrilateral
          ! Node numbering: bottom-left, bottom-right, top-right, top-left
          elemConn(4*(elemIdx-1)+1) = (j-1)*(nx+1) + i      ! bottom-left
          elemConn(4*(elemIdx-1)+2) = (j-1)*(nx+1) + (i+1)  ! bottom-right
          elemConn(4*(elemIdx-1)+3) = j*(nx+1) + (i+1)      ! top-right
          elemConn(4*(elemIdx-1)+4) = j*(nx+1) + i          ! top-left
        end do
      end do

      ! Add nodes to mesh
      call ESMF_MeshAddNodes(mesh, nodeIds, nodeCoords, nodeOwners, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        deallocate(nodeCoords, nodeIds, nodeOwners, elemIds, elemTypes, elemConn)
        return
      end if

      ! Add elements to mesh
      call ESMF_MeshAddElements(mesh, elemIds, elemTypes, elemConn, rc=rc)

      ! Clean up
      deallocate(nodeCoords, nodeIds, nodeOwners, elemIds, elemTypes, elemConn)
    else
      ! Other PETs contribute empty arrays
      allocate(nodeCoords(0), nodeIds(0), nodeOwners(0))
      allocate(elemIds(0), elemTypes(0), elemConn(0))

      call ESMF_MeshAddNodes(mesh, nodeIds, nodeCoords, nodeOwners, rc=rc)
      if (rc == ESMF_SUCCESS) then
        call ESMF_MeshAddElements(mesh, elemIds, elemTypes, elemConn, rc=rc)
      end if

      deallocate(nodeCoords, nodeIds, nodeOwners, elemIds, elemTypes, elemConn)
    end if

  end subroutine CreateMeshFromGridCoords

  !> @brief Convert C string to Fortran string
  function cstr_to_fstr(cstr) result(fstr)
    type(c_ptr), value :: cstr
    character(len=:), allocatable :: fstr
    integer :: i, len
    character(1), pointer :: carray(:)
    if (.not. c_associated(cstr)) then
      fstr = ""
      return
    end if
    call c_f_pointer(cstr, carray, [1000])
    len = 0
    do i = 1, 1000
      if (carray(i) == char(0)) exit
      len = len + 1
    end do
    if (len > 0) then
      allocate(character(len=len) :: fstr)
      fstr = ""
      do i = 1, len
        fstr(i:i) = carray(i)
      end do
    else
      fstr = ""
    end if
  end function cstr_to_fstr

  !> @brief Dynamically advertise ImportState fields from meteorology registry
  subroutine advertise_met_registry_fields(importState, rc)
    type(ESMF_State), intent(inout) :: importState
    integer, intent(out) :: rc

    integer(c_size_t) :: nvars, n_alias, i, j
    character(len=:), allocatable :: internal_name, alias
    type(ESMF_Field) :: field

    rc = ESMF_SUCCESS

    ! Get number of meteorology variables in registry
    nvars = cece_get_met_registry_count()
    write(*,'(A,I0,A)') "INFO: [CECE] Advertising ", nvars, " meteorology variables from registry"

    ! Loop through each internal meteorology variable
    do i = 0, nvars-1
      internal_name = cstr_to_fstr(cece_get_met_registry_internal_name(i))
      n_alias = cece_get_met_registry_alias_count(i)

      write(*,'(A,A,A,I0,A)') "INFO: [CECE] Internal name '", trim(internal_name), "' has ", n_alias, " aliases"

      ! Loop through each alias for this internal variable
      do j = 0, n_alias-1
        alias = cstr_to_fstr(cece_get_met_registry_alias(i, j))
        write(*,'(A,A,A)') "INFO: [CECE]   Advertising ImportState field: ", trim(alias)

        ! Create a field advertisement (this is just metadata, no actual allocation)
        field = ESMF_FieldEmptyCreate(name=trim(alias), rc=rc)
        if (rc /= ESMF_SUCCESS) then
          write(*,'(A,A,A,I0)') "ERROR: [CECE] Failed to create empty field for ", trim(alias), " rc=", rc
          return
        end if

        ! Add to ImportState
        call ESMF_StateAdd(importState, (/field/), rc=rc)
        if (rc /= ESMF_SUCCESS) then
          write(*,'(A,A,A,I0)') "ERROR: [CECE] Failed to add field ", trim(alias), " to ImportState rc=", rc
          call ESMF_FieldDestroy(field, rc=rc)
          return
        end if
      end do
    end do

    write(*,'(A)') "INFO: [CECE] Successfully advertised all meteorology registry fields"

  end subroutine advertise_met_registry_fields

end module cece_cap_mod
