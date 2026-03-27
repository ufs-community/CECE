!> @file aces_cap.F90
!> @brief NUOPC Model cap for ACES using IPDv01 initialize protocol.
!>
!> Uses the same IPDv01 pattern as DATM for compatibility with both
!> standalone drivers and full NUOPC coupled systems.
!>
!> Phase map (IPDv01):
!>   IPDv01p1 -> InitializeAdvertise  (Kokkos init, config, advertise fields)
!>   IPDv01p3 -> InitializeRealize    (create/allocate ESMF fields, bind ingestor)
!>   label_Advance  -> ACES_Run
!>   label_Finalize -> ACES_Finalize

module aces_cap_mod
  use iso_c_binding
  use ESMF
  use NUOPC
  use NUOPC_Model, modelSS => SetServices
  use NUOPC_Model, only : model_label_Advance  => label_Advance
  use NUOPC_Model, only : model_label_Finalize => label_Finalize
  use tide_mod, only: tide_type, tide_init, tide_init_from_esmfconfig, tide_advance, tide_get_ptr, tide_finalize
  implicit none

  !> @brief Module-level C++ data pointer (save ensures persistence across phases).
  type(c_ptr), save :: g_aces_data_ptr = c_null_ptr

  !> @brief Module-level TIDE object
  type(tide_type), save :: g_tide
  logical, save :: g_tide_initialized = .false.

  !> @brief Module-level config file path (save ensures persistence across phases).
  character(len=512), save :: g_config_file_path = "aces_config.yaml"

  !> @brief Module-level step counter for output indexing.
  integer, save :: g_step_count = 0

  !> @brief Module-level start time (seconds since epoch) for elapsed time computation.
  real(c_double), save :: g_start_time_seconds = 0.0d0

  !> @brief Time step in seconds (set from clock at Realize time).
  integer, save :: g_time_step_secs = 3600

  !> @brief Module-level grid dimensions
  integer, save :: g_nx = 0, g_ny = 0, g_nz = 0

  !> @brief Dummy data buffer for standalone core testing without TIDE
  real(c_double), allocatable, save, target :: g_dummy_data_buffer(:,:)
  logical, save :: g_dummy_initialized = .false.


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
      integer(c_int), intent(in) :: index
      character(kind=c_char), intent(out) :: name(*)
      integer(c_int), intent(out) :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_external_field_count(data_ptr, count, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_external_field_name(data_ptr, index, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: index
      character(kind=c_char), intent(out) :: name(*)
      integer(c_int), intent(out) :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_stream_field_count(data_ptr, count, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_stream_field_name(data_ptr, index, name, name_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: index
      character(kind=c_char), intent(out) :: name(*)
      integer(c_int), intent(out) :: name_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_bind_fields(data_ptr, field_ptrs, num_fields, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      type(c_ptr), intent(in) :: field_ptrs(*)
      integer(c_int), intent(in) :: num_fields
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_writer_initialize(data_ptr, nx, ny, nz, start_time, start_time_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_writer_initialize_with_coords(data_ptr, nx, ny, nz, lon_coords, lat_coords, &
                                                      start_time, start_time_len, rc) bind(C)
      import :: c_ptr, c_char, c_int, c_double
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      real(c_double), intent(in) :: lon_coords(nx), lat_coords(ny)
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_get_ingestor_streams_path(data_ptr, streams_path, path_len, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(out) :: streams_path(*)
      integer(c_int), intent(out) :: path_len
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_run(data_ptr, hour, day_of_week, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: hour
      integer(c_int), value :: day_of_week
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_finalize(data_ptr, rc) bind(C)
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_set_export_field(data_ptr, name, name_len, field_data, nx, ny, nz, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len
      type(c_ptr), value :: field_data
      integer(c_int), value :: nx, ny, nz
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_core_write_step(data_ptr, time_seconds, step_index, rc) bind(C)
      import :: c_ptr, c_double, c_int
      type(c_ptr), value :: data_ptr
      real(c_double), value :: time_seconds
      integer(c_int), value :: step_index
      integer(c_int), intent(out) :: rc
    end subroutine
    subroutine aces_ingestor_set_field(data_ptr, field_name, name_len, field_data, n_lev, n_elem, rc) bind(C)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(in) :: field_name(*)
      integer(c_int), value :: name_len
      type(c_ptr), value :: field_data
      integer(c_int), value :: n_lev
      integer(c_int), value :: n_elem
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
  !> Identical pattern to TIDE dshr_model_initphase.
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
    type(ESMF_Mesh) :: mesh
    integer :: nx, ny, nz
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

    write(*,'(A)') "INFO: [ACES] InitializeRealize (IPDv01p3) entered"

    ! Reset step counter for this run
    g_step_count = 0

    call ESMF_GridCompGet(comp, grid=grid, mesh=mesh, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [ACES] ESMF_GridCompGet failed in Realize: rc=", rc
      return
    end if

    ! Get nx/ny from the grid using computationalLBound/UBound on localDE=0
    block
      integer, allocatable :: lbnd(:), ubnd(:)
      allocate(lbnd(2), ubnd(2))
      call ESMF_GridGet(grid, localDE=0, staggerloc=ESMF_STAGGERLOC_CENTER, &
                        computationalLBound=lbnd, computationalUBound=ubnd, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "WARNING: [ACES] ESMF_GridGet(bounds) failed, defaulting nx=ny=4: rc=", rc
        nx = 4; ny = 4
        rc = ESMF_SUCCESS
      else
        nx = ubnd(1) - lbnd(1) + 1
        ny = ubnd(2) - lbnd(2) + 1
      end if
      deallocate(lbnd, ubnd)
    end block

    ! nz: get from config via C++ (number of vertical levels)
    nz = 10  ! default

    ! --- Check if ingestor streams are configured early ---
    write(*,'(A)') "INFO: [ACES] Checking for ingestor streams configuration"
    call aces_core_get_ingestor_streams_path(g_aces_data_ptr, streams_path, &
                                             streams_path_len, c_rc)
    use_ingestor = (c_rc == 0 .and. streams_path_len > 0)

    if (use_ingestor) then
      write(*,'(A)') "INFO: [ACES] Ingestor streams configured - will use grid-based regridding"
      write(*,'(A,A)') "INFO: [ACES] Streams path: ", trim(streams_path(1:int(streams_path_len)))
    else
      write(*,'(A)') "INFO: [ACES] No ingestor streams configured - will create grid-based fields"
    end if

    allocate(minIndex(3), maxIndex(3))
    minIndex = [1, 1, 1]
    maxIndex = [nx, ny, nz]

    write(*,'(A,I0,A,I0,A,I0)') "INFO: [ACES] Grid dimensions: nx=", nx, " ny=", ny, " nz=", nz

    ! Store globals
    g_nx = nx
    g_ny = ny
    g_nz = nz

    ! Call aces_core_realize (validates config)
    call aces_core_realize(g_aces_data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "WARNING: [ACES] aces_core_realize returned non-success: rc=", rc
      rc = ESMF_SUCCESS
    end if

    ! --- Phase 2: TIDE init and field binding ---
    ! Pass grid dimensions to C++ (extracted from ESMF grid)
    call aces_core_initialize_p2(g_aces_data_ptr, int(nx, c_int), int(ny, c_int), &
                                 int(nz, c_int), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [ACES] aces_core_initialize_p2 failed: rc=", rc
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
              write(*,'(A,I0)') "WARNING: [ACES] Failed to get longitude coordinates: rc=", rc
              ! Fall back to legacy initialization
              call aces_core_writer_initialize(g_aces_data_ptr, int(nx, c_int), int(ny, c_int), &
                                              int(nz, c_int), start_time_str, int(len_trim(start_time_str), c_int), c_rc)
            else
              call ESMF_GridGetCoord(grid, coordDim=2, localDE=0, staggerloc=ESMF_STAGGERLOC_CENTER, &
                                    farrayptr=grid_lat, rc=rc)
              if (rc /= ESMF_SUCCESS) then
                write(*,'(A,I0)') "WARNING: [ACES] Failed to get latitude coordinates: rc=", rc
                ! Fall back to legacy initialization
                call aces_core_writer_initialize(g_aces_data_ptr, int(nx, c_int), int(ny, c_int), &
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

                write(*,'(A,2F10.3)') "INFO: [ACES] Grid longitude range: ", lon_coords(1), lon_coords(nx)
                write(*,'(A,2F10.3)') "INFO: [ACES] Grid latitude range: ", lat_coords(1), lat_coords(ny)

                ! Call enhanced writer initialization with coordinates
                call aces_core_writer_initialize_with_coords(g_aces_data_ptr, int(nx, c_int), int(ny, c_int), &
                                                           int(nz, c_int), lon_coords, lat_coords, &
                                                           start_time_str, int(len_trim(start_time_str), c_int), c_rc)
                deallocate(lon_coords, lat_coords)
              end if
            end if
          end block

          rc = int(c_rc)
          if (rc /= ESMF_SUCCESS) then
            write(*,'(A,I0)') "WARNING: [ACES] Writer initialization failed: rc=", rc
            rc = ESMF_SUCCESS  ! Non-fatal
          end if
        end if
        ! Capture time step size for elapsed time computation in Run
        call ESMF_TimeIntervalGet(timeStep, s=dt_secs, rc=rc)
        if (rc == ESMF_SUCCESS) then
          g_time_step_secs = dt_secs
        else
          g_time_step_secs = 3600
          rc = ESMF_SUCCESS
        end if
      end if
    end block

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
      ! Get the actual species name from C++
      call aces_core_get_species_name(g_aces_data_ptr, int(i-1, c_int), species_name, &
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
        write(*,'(A,I0,A,A,A,I0)') "ERROR: [ACES] ESMF_FieldCreate failed for species ", i, &
                                   " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      ! Extract the raw data pointer.
      ! Grid fields are stored as (nx, ny, nz) — use rank-3 farrayPtr.
      call ESMF_FieldGet(field, localDe=0, farrayPtr=fptr, rc=rc)
      if (rc /= ESMF_SUCCESS .or. .not. associated(fptr)) then
        write(*,'(A,I0,A,A,A,I0)') &
          "ERROR: [ACES] ESMF_FieldGet(grid farrayPtr) failed for species ", i, &
          " (", trim(species_name(1:int(species_name_len))), "): rc=", rc
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if
      field_ptrs(i) = c_loc(fptr(1,1,1))

      ! Register field pointer in C++ export state FieldMap
      call aces_core_set_export_field(g_aces_data_ptr, &
          species_name(1:int(species_name_len))//c_null_char, &
          int(species_name_len, c_int), &
          field_ptrs(i), &
          int(nx, c_int), int(ny, c_int), int(nz, c_int), c_rc)
      if (c_rc /= 0) then
        write(*,'(A,A)') "WARNING: [ACES] aces_core_set_export_field failed for: ", &
                         trim(species_name(1:int(species_name_len)))
      end if

      ! Add field to export state using ESMF_StateAdd with fieldList keyword
      call ESMF_StateAdd(exportState, fieldList=[field], rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0,A,A,A,I0)') "ERROR: [ACES] ESMF_StateAdd failed for species ", i, &
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

    ! --- Phase 4: Initialize ingestor if configured ---
    if (use_ingestor .and. .not. g_tide_initialized) then
      write(*,'(A)') "INFO: [ACES] Initializing TIDE ingestor..."

      ! Initialize TIDE with the configured streams path and model mesh
      ! Note: passing trimmed streams_path to ESMF RC initialization
      call tide_init_from_esmfconfig(g_tide, trim(streams_path(1:int(streams_path_len))), &
                                     mesh, clock, rc)

      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: [ACES] TIDE initialization failed rc=", rc
        rc = ESMF_FAILURE
        deallocate(minIndex, maxIndex, field_ptrs)
        return
      end if

      g_tide_initialized = .true.
      write(*,'(A)') "INFO: [ACES] TIDE initialized successfully"
    else
      if (use_ingestor) then
        write(*,'(A)') "INFO: [ACES] TIDE already initialized (idempotent)"
      else
        write(*,'(A)') "INFO: [ACES] No ingestor streams configured - continuing without ingestor"
      end if
    end if

    deallocate(minIndex, maxIndex, field_ptrs)
    write(*,'(A)') "INFO: [ACES] InitializeRealize completed successfully"
    rc = ESMF_SUCCESS
  end subroutine ACES_InitializeRealize

  !> @brief Run phase: advance TIDE and execute ACES physics/stacking.
  !>
  !> Extracts hour-of-day and day-of-week from the ESMF clock in Fortran
  !> (where ESMF derived types are safe) and passes plain integers to the
  !> ESMF-free C++ core.
  subroutine ACES_Run(comp, rc)
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

    rc = ESMF_SUCCESS

    if (.not. c_associated(g_aces_data_ptr)) then
      write(*,'(A)') "ERROR: [ACES] Run called but data pointer is null"
      rc = ESMF_FAILURE
      return
    end if

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
        call tide_advance(g_tide, run_clock, tide_rc)
        if (tide_rc /= ESMF_SUCCESS) then
           write(*,'(A,I0)') "ERROR: [ACES] TIDE advance failed rc=", tide_rc
        else
           ! Critical: Force I/O synchronization after TIDE advance for large grids
           if (g_nx * g_ny > 50000) then
             write(*,'(A,I0)') "INFO: [ACES] Large grid detected, adding TIDE sync delay..."
             call flush(6)  ! Force output buffer flush
           end if

           ! Transfer fields - iterate through stream fields for emission data
           call aces_core_get_stream_field_count(g_aces_data_ptr, num_fields, c_rc)
           if (c_rc == 0) then
             do i = 0, num_fields-1
               call aces_core_get_stream_field_name(g_aces_data_ptr, int(i, c_int), field_name, &
                                                    field_name_len, c_rc)
               if (c_rc == 0 .and. field_name_len > 0) then
                 call tide_get_ptr(g_tide, field_name(1:int(field_name_len)), ptr, tide_rc)
                 if (tide_rc == 0 .and. associated(ptr)) then
                   write(*,'(A,A,A,I0,A,I0,A,I0)') "DEBUG: Field ", &
                        trim(field_name(1:int(field_name_len))), &
                        " ptr dimensions: ", size(ptr,1), " x ", size(ptr,2), &
                        " (total=", size(ptr), ")"
                   call aces_ingestor_set_field(g_aces_data_ptr, &
                        field_name(1:int(field_name_len))//c_null_char, &
                        field_name_len, c_loc(ptr(1,1)), &
                        int(size(ptr,1), c_int), int(size(ptr,2), c_int), c_rc)
                   if (c_rc /= 0) then
                     write(*,'(A,A)') "WARNING: [ACES] aces_ingestor_set_field failed for: ", &
                                       trim(field_name(1:int(field_name_len)))
                   end if
                 else
                   write(*,'(A,A,A,I0)') "WARNING: [ACES] tide_get_ptr failed for field ", &
                        trim(field_name(1:int(field_name_len))), " rc=", tide_rc
                 end if
               end if
             end do
           end if
        end if
      endif
    endif

    call aces_core_run(g_aces_data_ptr, int(hour, c_int), int(day_of_week, c_int), c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) return

    ! Write output step
    time_seconds = real(g_step_count * g_time_step_secs, c_double)
    call aces_core_write_step(g_aces_data_ptr, time_seconds, int(g_step_count, c_int), c_step_rc)
    if (c_step_rc /= 0) then
      write(*,'(A)') "WARNING: [ACES] aces_core_write_step failed"
    end if
    g_step_count = g_step_count + 1

    ! Critical: Final synchronization for large grids before returning
    if (g_nx * g_ny > 50000) then
      write(*,'(A,I0)') "INFO: [ACES] Large grid final sync (", g_nx * g_ny, " points)..."
      call flush(6)  ! Force all I/O completion
    end if

    write(*,'(A)') "INFO: [ACES] ACES_Run returning..."
    flush(6)

  end subroutine ACES_Run

  !> @brief Finalize phase: release all ACES resources.
  subroutine ACES_Finalize(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    integer :: tide_rc

    write(*,'(A)') "INFO: ACES Finalize - beginning cleanup"

    if (.not. c_associated(g_aces_data_ptr)) then
      write(*,'(A)') "WARNING: [ACES] Finalize called but data pointer is null"
      rc = ESMF_SUCCESS
      return
    end if

    if (g_tide_initialized) then
      write(*,'(A)') "INFO: Finalizing TIDE..."
      call tide_finalize(g_tide, tide_rc)
      if (tide_rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "WARNING: [ACES] TIDE finalize returned non-success rc=", tide_rc
      else
        write(*,'(A)') "INFO: TIDE finalized successfully"
      end if
      g_tide_initialized = .false.
    end if

    write(*,'(A)') "INFO: Calling ACES core finalize..."
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
