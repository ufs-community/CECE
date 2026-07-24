!> @file cece_cap.F90
!> @brief Production-grade decoupled NUOPC Model cap for CECE.
module cece_cap_mod
  use iso_c_binding
  use ESMF
  use NUOPC
  use NUOPC_Model, modelSS => SetServices
  use NUOPC_Model, only: &
    label_Advertise, &
    label_RealizeProvided, &
    model_label_Advance => label_Advance, &
    model_label_Finalize => label_Finalize
  implicit none

  private

  public :: CECE_SetServices, CECE_SetConfigPath

  !> @brief Module-level C++ data pointers (save ensures persistence across phases).
  type(c_ptr), save :: g_cece_data_ptr = c_null_ptr
  type(c_ptr), save :: g_driver_ptr = c_null_ptr

  !> @brief Module-level config file path (save ensures persistence across phases).
  character(len=512), save :: g_config_file_path = "cece_control_mock.yaml"

  !> @brief Module-level step counter for output indexing.
  integer, save :: g_step_count = 0

  !> @brief Module-level start time (seconds since epoch) for elapsed time computation.
  real(c_double), save :: g_start_time_seconds = 0.0d0

  !> @brief Time step in seconds (set from clock at Realize time).
  real(c_double), save :: g_time_step_secs = 3600.0d0

  !> @brief Module-level grid dimensions
  integer, save :: g_nx = 0, g_ny = 0, g_nz = 0

  ! C interfaces to the C++ CECE Core and Driver libraries
  interface
    subroutine cece_set_config_file_path(config_path, path_len) &
                                         bind(C, name="cece_set_config_file_path")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
    end subroutine

    subroutine cece_run_log_setup(config_path, path_len) &
                                  bind(C, name="cece_run_log_setup")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
    end subroutine

    subroutine cece_core_initialize_p1(data_ptr, rc) &
                                        bind(C, name="cece_core_initialize_p1")
      import :: c_ptr, c_int
      type(c_ptr), intent(out) :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_realize(data_ptr, rc) &
                                 bind(C, name="cece_core_realize")
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_initialize_p2(data_ptr, nx, ny, nz, rc) &
                                        bind(C, name="cece_core_initialize_p2")
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(in) :: nx, ny, nz
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_get_grid_config(data_ptr, nx, ny, nz, &
                                         lon_min, lon_max, lat_min, lat_max, rc) &
                                         bind(C, name="cece_core_get_grid_config")
      import :: c_ptr, c_int, c_double
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: nx, ny, nz
      real(c_double), intent(out) :: lon_min, lon_max, lat_min, lat_max
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_get_timing_config(data_ptr, start_time, end_time, &
                                           timestep_seconds, max_len, rc) &
                                           bind(C, name="cece_core_get_timing_config")
      import :: c_ptr, c_int, c_char
      type(c_ptr), value :: data_ptr
      character(kind=c_char), intent(out) :: start_time(*), end_time(*)
      integer(c_int), intent(out) :: timestep_seconds
      integer(c_int), value, intent(in) :: max_len
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_run(data_ptr, hour, day_of_week, rc) &
                             bind(C, name="cece_core_run")
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: hour, day_of_week
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_finalize(data_ptr, rc) &
                                  bind(C, name="cece_core_finalize")
      import :: c_ptr, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_driver_create(yaml_path, path_len, nx, ny, nz, &
                                  lon_coords, lon_len, lat_coords, lat_len, mpi_comm_f, driver_ptr, rc) &
                                  bind(C, name="cece_driver_create")
      import :: c_ptr, c_char, c_int, c_double
      character(kind=c_char), intent(in) :: yaml_path(*)
      integer(c_int), value, intent(in) :: path_len
      integer(c_int), value, intent(in) :: nx, ny, nz
      real(c_double), intent(in) :: lon_coords(*), lat_coords(*)
      integer(c_int), value, intent(in) :: lon_len, lat_len
      integer(c_int), value, intent(in) :: mpi_comm_f
      type(c_ptr), intent(out) :: driver_ptr
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_driver_advance_time(driver_ptr, time_iso, time_len, &
                                        core_data_ptr, rc) &
                                        bind(C, name="cece_driver_advance_time")
      import :: c_ptr, c_char, c_int
      type(c_ptr), value, intent(in) :: driver_ptr
      character(kind=c_char), intent(in) :: time_iso(*)
      integer(c_int), value, intent(in) :: time_len
      type(c_ptr), value, intent(in) :: core_data_ptr
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_driver_destroy(driver_ptr) &
                                   bind(C, name="cece_driver_destroy")
      import :: c_ptr
      type(c_ptr), value, intent(in) :: driver_ptr
    end subroutine

    subroutine cece_core_writer_initialize_with_coords(data_ptr, nx, ny, nz, &
                                                       lon_coords, lon_len, lat_coords, lat_len, &
                                                       start_time, start_time_len, &
                                                       mpi_comm_f, rc) &
                                                       bind(C, name="cece_core_writer_initialize_with_coords")
      import :: c_ptr, c_char, c_int, c_double
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      real(c_double), intent(in) :: lon_coords(*), lat_coords(*)
      integer(c_int), value :: lon_len, lat_len
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), value :: mpi_comm_f
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_writer_initialize(data_ptr, nx, ny, nz, &
                                           start_time, start_time_len, &
                                           mpi_comm_f, rc) &
                                           bind(C, name="cece_core_writer_initialize")
      import :: c_ptr, c_char, c_int
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny, nz
      character(kind=c_char), intent(in) :: start_time(*)
      integer(c_int), value :: start_time_len
      integer(c_int), value :: mpi_comm_f
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_core_write_step(data_ptr, time_seconds, step_index, rc) &
                                    bind(C, name="cece_core_write_step")
      import :: c_ptr, c_int, c_double
      type(c_ptr), value :: data_ptr
      real(c_double), value :: time_seconds
      integer(c_int), value :: step_index
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

contains

  !> @brief Set the YAML config file path dynamically from parent driver
  subroutine CECE_SetConfigPath(config_path, rc)
    character(len=*), intent(in) :: config_path
    integer, intent(out) :: rc
    g_config_file_path = config_path
    rc = ESMF_SUCCESS
  end subroutine CECE_SetConfigPath

  !> @brief SetServices routine for the production CECE component
  subroutine CECE_SetServices(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    write(*,'(A)') "INFO: [Cap] CECE_SetServices entered"
    rc = ESMF_SUCCESS

    ! 1. Inherit NUOPC Model base services
    write(*,'(A)') "INFO: [Cap] Calling NUOPC_CompDerive..."
    call NUOPC_CompDerive(gcomp, modelSS, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [Cap] NUOPC_CompDerive failed rc=", rc
      return
    end if

    ! 2. Register initialization phase 1 (Advertise)
    write(*,'(A)') "INFO: [Cap] Specializing Initialize phase 1 (Advertise)..."
    call NUOPC_CompSpecialize(gcomp, specLabel=label_Advertise, &
      specRoutine=InitializeAdvertise, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [Cap] CompSpecialize(Advertise) failed rc=", rc
      return
    end if

    ! 3. Register initialization phase 2 (Realize)
    write(*,'(A)') "INFO: [Cap] Specializing Initialize phase 2 (Realize)..."
    call NUOPC_CompSpecialize(gcomp, specLabel=label_RealizeProvided, &
      specRoutine=InitializeRealize, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [Cap] CompSpecialize(Realize) failed rc=", rc
      return
    end if

    ! 4. Register Run (Advance) phase
    write(*,'(A)') "INFO: [Cap] Specializing Run (Advance) phase..."
    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Advance, &
      specRoutine=Run, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [Cap] CompSpecialize(Advance) failed rc=", rc
      return
    end if

    ! 5. Register Finalize phase
    write(*,'(A)') "INFO: [Cap] Specializing Finalize phase..."
    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Finalize, &
      specRoutine=Finalize, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [Cap] CompSpecialize(Finalize) failed rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [Cap] CECE_SetServices completed successfully"
  end subroutine CECE_SetServices

  !> @brief InitializeAdvertise (IPDv01p1)
  subroutine InitializeAdvertise(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc

    rc = ESMF_SUCCESS
    write(*,'(A)') "INFO: [Cap] InitializeAdvertise entered"

    ! Set YAML configuration path in the core C-API
    call cece_set_config_file_path(trim(g_config_file_path)//c_null_char, &
                                   int(len_trim(g_config_file_path), c_int))

    ! Configure run logging (optional log file, per-rank stdout suppression) and
    ! print the startup banner. Shared with the standalone driver so behavior is
    ! identical regardless of how CECE is launched.
    call cece_run_log_setup(trim(g_config_file_path)//c_null_char, &
                            int(len_trim(g_config_file_path), c_int))

    ! Allocate core C++ data structures
    call cece_core_initialize_p1(g_cece_data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [Cap] Failed to initialize CECE Core P1"
      return
    end if

    ! Realize config validations
    call cece_core_realize(g_cece_data_ptr, c_rc)
    rc = int(c_rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [Cap] Failed to realize configuration"
      return
    end if

    write(*,'(A)') "INFO: [Cap] InitializeAdvertise completed successfully"
  end subroutine InitializeAdvertise

  !> @brief InitializeRealize (IPDv01p3)
  subroutine InitializeRealize(comp, rc)
    type(ESMF_GridComp) :: comp
    integer, intent(out) :: rc

    type(ESMF_Grid) :: grid
    type(ESMF_VM) :: vm
    type(ESMF_Time) :: currTime
    integer :: mpi_comm_val
    character(len=64) :: start_time_str, end_time_str
    integer(c_int) :: timestep_seconds_c
    real(c_double) :: lon_min, lon_max, lat_min, lat_max
    real(c_double) :: lon_step, lat_step
    real(c_double), allocatable, target :: lon_coords(:), lat_coords(:)
    integer(c_int) :: c_rc
    integer :: i, j

    rc = ESMF_SUCCESS
    write(*,'(A)') "INFO: [Cap] InitializeRealize entered"

    ! 1. Fetch grid configuration dimensions and extents from C++ core
    call cece_core_get_grid_config(g_cece_data_ptr, g_nx, g_ny, g_nz, &
                                   lon_min, lon_max, lat_min, lat_max, c_rc)
    if (c_rc /= 0) then
      write(*,'(A)') "ERROR: [Cap] Failed to retrieve grid configuration"
      rc = ESMF_FAILURE
      return
    end if

    write(*,'(A,I0,A,I0,A,I0)') "INFO: [Cap] Grid Configuration: ", g_nx, "x", g_ny, "x", g_nz

    ! 2. Complete Phase 2 grid-binding on the compute core
    call cece_core_initialize_p2(g_cece_data_ptr, g_nx, g_ny, g_nz, c_rc)
    if (c_rc /= 0) then
      write(*,'(A)') "ERROR: [Cap] Failed to initialize CECE Core P2"
      rc = ESMF_FAILURE
      return
    end if

    ! 3. Create the ESMF uniform Grid
    grid = ESMF_GridCreateNoPeriDimUfrm(maxIndex=(/g_nx, g_ny/), &
      minCornerCoord=(/lon_min, lat_min/), &
      maxCornerCoord=(/lon_max, lat_max/), &
      coordSys=ESMF_COORDSYS_SPH_DEG, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: [Cap] Failed to create ESMF grid rc=", rc
      return
    end if

    call ESMF_GridCompSet(comp, grid=grid, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! 4. Generate local coordinate arrays to pass to the driver facade
    allocate(lon_coords(g_nx))
    allocate(lat_coords(g_ny))

    lon_step = (lon_max - lon_min) / real(g_nx, c_double)
    do i = 1, g_nx
      lon_coords(i) = lon_min + (real(i, c_double) - 0.5D0) * lon_step
    end do

    lat_step = (lat_max - lat_min) / real(g_ny, c_double)
    do j = 1, g_ny
      lat_coords(j) = lat_min + (real(j, c_double) - 0.5D0) * lat_step
    end do

    ! Retrieve the raw ESMF VM MPI communicator
    call ESMF_GridCompGet(comp, vm=vm, rc=rc)
    if (rc == ESMF_SUCCESS) then
      call ESMF_VMGet(vm, mpiCommunicator=mpi_comm_val, rc=rc)
    else
      mpi_comm_val = 0 ! Default fallback
    end if

    ! 5. Create the cece_driver orchestrator facade
    call cece_driver_create(trim(g_config_file_path)//c_null_char, &
                            int(len_trim(g_config_file_path), c_int), &
                            int(g_nx, c_int), int(g_ny, c_int), int(g_nz, c_int), &
                            lon_coords, int(g_nx, c_int), &
                            lat_coords, int(g_ny, c_int), &
                            int(mpi_comm_val, c_int), g_driver_ptr, c_rc)
    if (c_rc /= 0) then
      write(*,'(A)') "ERROR: [Cap] Failed to create C++ Driver Facade"
      rc = ESMF_FAILURE
      return
    end if

    ! 6. Retrieve timing and start output writing from C++ Core
    call cece_core_get_timing_config(g_cece_data_ptr, start_time_str, end_time_str, &
                                     timestep_seconds_c, 64, c_rc)
    if (c_rc /= 0) then
      write(*,'(A)') "ERROR: [Cap] Failed to retrieve timing config from core"
      rc = ESMF_FAILURE
      return
    end if

    g_time_step_secs = real(timestep_seconds_c, c_double)

    ! Parse start time string into ESMF Time to extract seconds
    call ESMF_TimeSet(currTime, timeString=trim(start_time_str), rc=rc)
    if (rc == ESMF_SUCCESS) then
      call ESMF_TimeGet(currTime, s_r8=g_start_time_seconds, rc=rc)
    end if

    call cece_core_writer_initialize_with_coords(g_cece_data_ptr, &
                                                 int(g_nx, c_int), int(g_ny, c_int), int(g_nz, c_int), &
                                                 lon_coords, int(g_nx, c_int), &
                                                 lat_coords, int(g_ny, c_int), &
                                                 trim(start_time_str)//c_null_char, &
                                                 int(len_trim(start_time_str), c_int), &
                                                 int(mpi_comm_val, c_int), c_rc)
    if (c_rc /= 0) then
      write(*,'(A)') "ERROR: [Cap] Failed to initialize Core Output Writer"
      rc = ESMF_FAILURE
      return
    end if

    deallocate(lon_coords)
    deallocate(lat_coords)

    write(*,'(A)') "INFO: [Cap] InitializeRealize completed successfully"
  end subroutine InitializeRealize

  !> @brief Run Advance step (Specialized via model_label_Advance)
  subroutine Run(comp, rc)
    type(ESMF_GridComp) :: comp
    integer, intent(out) :: rc

    type(ESMF_Clock) :: clock
    type(ESMF_Time) :: currTime
    character(len=64) :: time_str
    real(c_double) :: current_seconds, elapsed_seconds
    integer :: hour, day_of_week
    integer(c_int) :: c_rc

    rc = ESMF_SUCCESS

    write(*,'(A,L1)') "DEBUG: [Cap] g_driver_ptr associated? ", c_associated(g_driver_ptr)
    write(*,'(A,L1)') "DEBUG: [Cap] g_cece_data_ptr associated? ", c_associated(g_cece_data_ptr)

    call ESMF_GridCompGet(comp, clock=clock, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    call ESMF_ClockGet(clock, currTime=currTime, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    call ESMF_TimeGet(currTime, timeString=time_str, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! A. Call the C++ driver facade to ingest/regrid all offline datasets
    call cece_driver_advance_time(g_driver_ptr, trim(time_str)//c_null_char, &
                                  int(len_trim(time_str), c_int), g_cece_data_ptr, c_rc)
    if (c_rc /= 0) then
      write(*,'(A)') "ERROR: [Cap] cece_driver_advance_time failed"
      rc = ESMF_FAILURE
      return
    end if

    ! B. Extract timing properties and run the core chemistry compute
    call ESMF_TimeGet(currTime, dayOfWeek=day_of_week, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    call ESMF_TimeGet(currTime, h=hour, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    call cece_core_run(g_cece_data_ptr, int(hour, c_int), int(day_of_week, c_int), c_rc)
    if (c_rc < 0) then
      write(*,'(A)') "ERROR: [Cap] cece_core_run compute failed"
      rc = ESMF_FAILURE
      return
    end if

    ! C. Write standalone outputs
    call ESMF_TimeGet(currTime, s_r8=current_seconds, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    elapsed_seconds = current_seconds - g_start_time_seconds
    call cece_core_write_step(g_cece_data_ptr, elapsed_seconds, int(g_step_count, c_int), c_rc)

    g_step_count = g_step_count + 1
  end subroutine Run

  !> @brief Finalize and cleanup resources (Specialized via model_label_Finalize)
  subroutine Finalize(comp, rc)
    type(ESMF_GridComp) :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc

    rc = ESMF_SUCCESS
    write(*,'(A)') "INFO: [Cap] Finalizing CECE NUOPC Cap..."

    ! 1. Destroy C++ Driver Facade
    call cece_driver_destroy(g_driver_ptr)
    g_driver_ptr = c_null_ptr

    ! 2. Finalize and delete C++ core data structures
    call cece_core_finalize(g_cece_data_ptr, c_rc)
    g_cece_data_ptr = c_null_ptr

    write(*,'(A)') "INFO: [Cap] CECE NUOPC Cap finalized successfully"
  end subroutine Finalize

end module cece_cap_mod
