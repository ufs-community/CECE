!> @file single_driver.F90
!> @brief NUOPC Single-Model Driver for ACES standalone execution.
!>
!> Implements a NUOPC-compliant single-model driver hosting the ACES emissions
!> component for standalone testing and future coupling readiness.
!>
!> Phase execution order (NUOPC_Model specialization pattern):
!>   ESMF_GridCompInitialize phase=1  -> NUOPC Advertise (ModelBase_Advertise)
!>   ESMF_GridCompInitialize phase=2  -> NUOPC Realize   (ModelBase_Realize)
!>   ESMF_GridCompInitialize phase=3  -> ACES InitializeP1 (IPDv00p1)
!>   ESMF_GridCompInitialize phase=4  -> ACES InitializeP2 (IPDv00p2)
!>   ESMF_GridCompRun        phase=1  -> ACES Run (model_label_Advance)
!>   ESMF_GridCompFinalize   phase=1  -> ACES Finalize (model_label_Finalize)
!>
!> Usage:
!>   ./aces_nuopc_single_driver [options]
!>
!> Options:
!>   --config <path>       ACES YAML config file (default: aces_config.yaml)
!>   --streams <path>      CDEPS streams file (default: aces_emissions.streams)
!>   --start-time <ISO>    Start time YYYY-MM-DDTHH:MM:SS (default: 2020-01-01T00:00:00)
!>   --end-time <ISO>      End time YYYY-MM-DDTHH:MM:SS (default: 2020-01-02T00:00:00)
!>   --time-step <secs>    Time step in seconds (default: 3600)
!>   --nx <int>            Grid points in X direction (default: 4)
!>   --ny <int>            Grid points in Y direction (default: 4)
!>
!> Requirements: 2.1-2.15

program aces_nuopc_single_driver
  use ESMF
  use NUOPC
  use aces_cap_mod
  implicit none

  !--- ESMF component and infrastructure handles ---
  type(ESMF_GridComp) :: acesComp
  type(ESMF_State)    :: importState, exportState
  type(ESMF_Clock)    :: clock
  type(ESMF_Time)     :: startTime, stopTime
  type(ESMF_TimeInterval) :: timeStep
  type(ESMF_Calendar) :: calendar
  type(ESMF_Grid)     :: grid

  !--- Command-line configuration (with defaults) ---
  character(len=512) :: config_file    = "aces_config.yaml"
  character(len=512) :: streams_file   = "aces_emissions.streams"
  character(len=64)  :: start_time_str = "2020-01-01T00:00:00"
  character(len=64)  :: end_time_str   = "2020-01-02T00:00:00"
  integer            :: time_step_secs = 3600
  integer            :: nx = 4
  integer            :: ny = 4

  !--- Parsed time components ---
  integer :: start_yy, start_mm, start_dd, start_hh, start_mn, start_ss
  integer :: stop_yy,  stop_mm,  stop_dd,  stop_hh,  stop_mn,  stop_ss

  !--- Runtime state ---
  integer :: rc, step, total_steps
  logical :: clock_done
  integer, dimension(2) :: maxIndex2D
  character(len=ESMF_MAXSTR) :: msg

  ! -----------------------------------------------------------------------
  ! 1. Parse command-line arguments
  ! -----------------------------------------------------------------------
  call parse_command_line(config_file, streams_file, start_time_str, end_time_str, &
                          time_step_secs, nx, ny)

  ! -----------------------------------------------------------------------
  ! 2. Initialize ESMF (Requirement 2.14: no ESMF errors in JCSDA Docker)
  ! -----------------------------------------------------------------------
  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_GREGORIAN, &
                       defaultLogFileName="aces_nuopc_driver.log", &
                       logkindflag=ESMF_LOGKIND_MULTI, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(*,'(A)') "ERROR: [Driver] ESMF_Initialize failed"
    stop 1
  end if
  write(*,'(A)') "INFO: [Driver] ESMF initialized"

  ! -----------------------------------------------------------------------
  ! 3. Create ESMF_Clock with configurable parameters (Requirements 2.3, 2.11, 2.12)
  ! -----------------------------------------------------------------------
  call parse_iso8601(start_time_str, start_yy, start_mm, start_dd, &
                     start_hh, start_mn, start_ss)
  call parse_iso8601(end_time_str,   stop_yy,  stop_mm,  stop_dd,  &
                     stop_hh,  stop_mn,  stop_ss)

  calendar = ESMF_CalendarCreate(ESMF_CALKIND_GREGORIAN, name="Gregorian", rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to create calendar", rc)

  call ESMF_TimeSet(startTime, yy=start_yy, mm=start_mm, dd=start_dd, &
                    h=start_hh, m=start_mn, s=start_ss, &
                    calendar=calendar, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to set start time", rc)

  call ESMF_TimeSet(stopTime, yy=stop_yy, mm=stop_mm, dd=stop_dd, &
                    h=stop_hh, m=stop_mn, s=stop_ss, &
                    calendar=calendar, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to set stop time", rc)

  call ESMF_TimeIntervalSet(timeStep, s=time_step_secs, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to set time step", rc)

  clock = ESMF_ClockCreate(timeStep, startTime, stopTime=stopTime, &
                            name="ACES_Clock", rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to create clock", rc)

  write(msg,'(A,I4.4,A,I2.2,A,I2.2,A,I4.4,A,I2.2,A,I2.2,A,I0,A)') &
    "INFO: [Driver] Clock: ", start_yy, "-", start_mm, "-", start_dd, &
    " -> ", stop_yy, "-", stop_mm, "-", stop_dd, &
    " dt=", time_step_secs, "s"
  write(*,'(A)') trim(msg)

  ! -----------------------------------------------------------------------
  ! 4. Create ESMF_Grid for spatial discretization (Requirement 2.4)
  ! -----------------------------------------------------------------------
  maxIndex2D = [nx, ny]
  grid = ESMF_GridCreateNoPeriDim(maxIndex=maxIndex2D, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to create grid", rc)

  write(msg,'(A,I0,A,I0,A)') "INFO: [Driver] Grid created: ", nx, " x ", ny, " (nx x ny)"
  write(*,'(A)') trim(msg)

  ! -----------------------------------------------------------------------
  ! 5. Create ESMF States
  ! -----------------------------------------------------------------------
  importState = ESMF_StateCreate(name="ACES_ImportState", &
                                  stateintent=ESMF_STATEINTENT_IMPORT, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to create import state", rc)

  exportState = ESMF_StateCreate(name="ACES_ExportState", &
                                  stateintent=ESMF_STATEINTENT_EXPORT, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to create export state", rc)

  ! -----------------------------------------------------------------------
  ! 6. Create ACES GridComp and register via SetServices (Requirements 2.1, 2.2)
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [Driver] Creating ACES component"
  acesComp = ESMF_GridCompCreate(name="ACES", contextflag=ESMF_CONTEXT_PARENT_VM, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to create ACES GridComp", rc)

  ! Attach grid so Realize phase can access dimensions
  call ESMF_GridCompSet(acesComp, grid=grid, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to attach grid to ACES component", rc)

  ! Register all ACES phase methods (Advertise, Realize, InitP1, InitP2, Run, Finalize)
  write(*,'(A)') "INFO: [Driver] Calling ESMF_GridCompSetServices"
  call ESMF_GridCompSetServices(acesComp, ACES_SetServices, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("ESMF_GridCompSetServices failed", rc)

  ! -----------------------------------------------------------------------
  ! 7. NUOPC Phase: Advertise (Requirement 2.5)
  !    NUOPC_Model phase 1 = ModelBase_Advertise
  !    Components declare import/export field names without allocating memory
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [Driver] === Phase: Advertise ==="
  call ESMF_GridCompInitialize(acesComp, importState=importState, &
                                exportState=exportState, clock=clock, &
                                phase=1, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("ACES Advertise phase failed", rc)
  write(*,'(A)') "INFO: [Driver] Advertise phase complete"

  ! -----------------------------------------------------------------------
  ! 8. NUOPC Phase: Realize (Requirement 2.6)
  !    NUOPC_Model phase 2 = ModelBase_Realize
  !    Components create and allocate fields; import fields verified
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [Driver] === Phase: Realize ==="
  call ESMF_GridCompInitialize(acesComp, importState=importState, &
                                exportState=exportState, clock=clock, &
                                phase=2, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("ACES Realize phase failed", rc)
  write(*,'(A)') "INFO: [Driver] Realize phase complete"

  ! -----------------------------------------------------------------------
  ! 9. NUOPC Phase: Initialize P1 - IPDv00p1 (Requirement 2.7)
  !    Kokkos init, YAML config parse, PhysicsFactory, StackingEngine
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [Driver] === Phase: Initialize P1 (IPDv00p1) ==="
  call ESMF_GridCompInitialize(acesComp, importState=importState, &
                                exportState=exportState, clock=clock, &
                                phase=3, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("ACES Initialize P1 (IPDv00p1) failed", rc)
  write(*,'(A)') "INFO: [Driver] Initialize P1 complete"

  ! -----------------------------------------------------------------------
  ! 10. NUOPC Phase: Initialize P2 - IPDv00p2 (Requirement 2.7)
  !     CDEPS initialization and field binding
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [Driver] === Phase: Initialize P2 (IPDv00p2) ==="
  call ESMF_GridCompInitialize(acesComp, importState=importState, &
                                exportState=exportState, clock=clock, &
                                phase=4, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("ACES Initialize P2 (IPDv00p2) failed", rc)
  write(*,'(A)') "INFO: [Driver] Initialize P2 complete"

  ! -----------------------------------------------------------------------
  ! 11. Run loop: advance clock and execute ACES each step (Requirements 2.8, 2.9)
  ! -----------------------------------------------------------------------
  total_steps = 0
  clock_done  = .false.
  clock_done = ESMF_ClockIsStopTime(clock, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("Failed to check initial clock stop time", rc)

  write(*,'(A)') "INFO: [Driver] === Phase: Run Loop ==="
  do while (.not. clock_done)
    total_steps = total_steps + 1
    write(msg,'(A,I0)') "INFO: [Driver] Run step ", total_steps
    write(*,'(A)') trim(msg)

    ! Execute ACES Run phase (model_label_Advance, phase=1)
    call ESMF_GridCompRun(acesComp, importState=importState, &
                          exportState=exportState, clock=clock, &
                          phase=1, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(msg,'(A,I0)') "ACES Run phase failed at step ", total_steps
      call driver_abort(trim(msg), rc)
    end if

    ! Advance clock after run
    call ESMF_ClockAdvance(clock, rc=rc)
    if (rc /= ESMF_SUCCESS) call driver_abort("Failed to advance clock", rc)

    clock_done = ESMF_ClockIsStopTime(clock, rc=rc)
    if (rc /= ESMF_SUCCESS) call driver_abort("Failed to check clock stop time", rc)
  end do

  write(msg,'(A,I0,A)') "INFO: [Driver] Run loop complete: ", total_steps, " steps"
  write(*,'(A)') trim(msg)

  ! -----------------------------------------------------------------------
  ! 12. NUOPC Phase: Finalize (Requirement 2.10)
  !     Release Kokkos views, CDEPS, and all resources
  ! -----------------------------------------------------------------------
  write(*,'(A)') "INFO: [Driver] === Phase: Finalize ==="
  call ESMF_GridCompFinalize(acesComp, importState=importState, &
                              exportState=exportState, clock=clock, &
                              phase=1, rc=rc)
  if (rc /= ESMF_SUCCESS) call driver_abort("ACES Finalize phase failed", rc)
  write(*,'(A)') "INFO: [Driver] Finalize phase complete"

  ! -----------------------------------------------------------------------
  ! 13. Destroy ESMF objects
  ! -----------------------------------------------------------------------
  call ESMF_StateDestroy(importState, rc=rc)
  call ESMF_StateDestroy(exportState, rc=rc)
  call ESMF_GridDestroy(grid, rc=rc)
  call ESMF_ClockDestroy(clock, rc=rc)
  call ESMF_CalendarDestroy(calendar, rc=rc)
  call ESMF_GridCompDestroy(acesComp, rc=rc)

  ! -----------------------------------------------------------------------
  ! 14. Finalize ESMF
  ! -----------------------------------------------------------------------
  call ESMF_Finalize(rc=rc)
  write(*,'(A)') "INFO: [Driver] ESMF finalized. Driver complete."

contains

  !> @brief Parse command-line arguments into driver configuration.
  !> Supports: --config, --streams, --start-time, --end-time, --time-step, --nx, --ny
  !> Requirement 2.11, 2.12, 2.13
  subroutine parse_command_line(cfg, streams, t_start, t_end, dt, gnx, gny)
    character(len=*), intent(inout) :: cfg, streams, t_start, t_end
    integer,          intent(inout) :: dt, gnx, gny

    integer :: i, nargs
    character(len=512) :: arg, val

    nargs = command_argument_count()
    i = 1
    do while (i <= nargs)
      call get_command_argument(i, arg)
      select case (trim(arg))
        case ("--config")
          i = i + 1; call get_command_argument(i, cfg)
        case ("--streams")
          i = i + 1; call get_command_argument(i, streams)
        case ("--start-time")
          i = i + 1; call get_command_argument(i, t_start)
        case ("--end-time")
          i = i + 1; call get_command_argument(i, t_end)
        case ("--time-step")
          i = i + 1; call get_command_argument(i, val); read(val, *) dt
        case ("--nx")
          i = i + 1; call get_command_argument(i, val); read(val, *) gnx
        case ("--ny")
          i = i + 1; call get_command_argument(i, val); read(val, *) gny
        case ("--help", "-h")
          call print_usage(); stop 0
        case default
          write(*,'(A,A)') "WARNING: [Driver] Unknown argument ignored: ", trim(arg)
      end select
      i = i + 1
    end do

    ! Log all configuration (Requirement 2.13)
    write(*,'(A,A)') "INFO: [Driver] Config file:   ", trim(cfg)
    write(*,'(A,A)') "INFO: [Driver] Streams file:  ", trim(streams)
    write(*,'(A,A)') "INFO: [Driver] Start time:    ", trim(t_start)
    write(*,'(A,A)') "INFO: [Driver] End time:      ", trim(t_end)
    write(*,'(A,I0)') "INFO: [Driver] Time step (s): ", dt
    write(*,'(A,I0,A,I0)') "INFO: [Driver] Grid size:     ", gnx, " x ", gny
  end subroutine parse_command_line

  !> @brief Parse an ISO 8601 datetime string (YYYY-MM-DDTHH:MM:SS).
  subroutine parse_iso8601(iso_str, yy, mm, dd, hh, mn, ss)
    character(len=*), intent(in)  :: iso_str
    integer,          intent(out) :: yy, mm, dd, hh, mn, ss

    character(len=64) :: s
    integer :: tpos

    s = trim(iso_str)
    read(s(1:4),  '(I4)') yy
    read(s(6:7),  '(I2)') mm
    read(s(9:10), '(I2)') dd

    tpos = index(s, 'T')
    if (tpos > 0 .and. len_trim(s) >= tpos + 7) then
      read(s(tpos+1:tpos+2), '(I2)') hh
      read(s(tpos+4:tpos+5), '(I2)') mn
      read(s(tpos+7:tpos+8), '(I2)') ss
    else
      hh = 0; mn = 0; ss = 0
    end if
  end subroutine parse_iso8601

  !> @brief Print usage information.
  subroutine print_usage()
    write(*,'(A)') "Usage: aces_nuopc_single_driver [options]"
    write(*,'(A)') ""
    write(*,'(A)') "Options:"
    write(*,'(A)') "  --config <path>       ACES YAML config file (default: aces_config.yaml)"
    write(*,'(A)') "  --streams <path>      CDEPS streams file (default: aces_emissions.streams)"
    write(*,'(A)') "  --start-time <ISO>    Start time YYYY-MM-DDTHH:MM:SS (default: 2020-01-01T00:00:00)"
    write(*,'(A)') "  --end-time <ISO>      End time YYYY-MM-DDTHH:MM:SS (default: 2020-01-02T00:00:00)"
    write(*,'(A)') "  --time-step <secs>    Time step in seconds (default: 3600)"
    write(*,'(A)') "  --nx <int>            Grid points in X (default: 4)"
    write(*,'(A)') "  --ny <int>            Grid points in Y (default: 4)"
    write(*,'(A)') "  --help, -h            Show this help"
  end subroutine print_usage

  !> @brief Abort with an error message, finalizing ESMF cleanly.
  subroutine driver_abort(message, rc_in)
    character(len=*), intent(in) :: message
    integer,          intent(in) :: rc_in
    integer :: rc_fin

    write(*,'(A,A,A,I0,A)') "ERROR: [Driver] ", trim(message), " (rc=", rc_in, ")"
    call ESMF_Finalize(endflag=ESMF_END_ABORT, rc=rc_fin)
    stop 1
  end subroutine driver_abort

end program aces_nuopc_single_driver
