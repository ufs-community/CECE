!> @file driver.F90
!> @brief NUOPC Driver component for CECE standalone execution.
!>
!> Specializes NUOPC_Driver to manage a single CECE model component.
!> Handles clock management, run loop, and component lifecycle.

module driver

  USE MPI
  use ESMF
  use NUOPC
  use NUOPC_Driver, driverSS => SetServices
  use NUOPC_Driver, only: driver_label_SetModelServices => label_SetModelServices
  use cece_cap_mod, ceceSS => CECE_SetServices
  use, intrinsic :: iso_c_binding

  implicit none

  private

  public SetServices, set_driver_config_file, set_cece_config_file

  !> @brief Driver config file path (.cfg format, for clock/grid setup)
  character(len=512), save :: g_driver_cfg_file = "cece_driver.cfg"

  !> @brief CECE config file path (.yaml format, for cap/physics/streams)
  character(len=512), save :: g_cece_yaml_file = "cece_config.yaml"

  ! NUOPC prototype pattern parameters (like SingleModelOpenMPProto)
  integer, parameter :: stepCount = 6  ! Default fallback
  real(ESMF_KIND_R8), parameter :: stepTime = 3600.D0  ! Default fallback (1 hour)

  ! C interface to read YAML timing config
  interface
    subroutine cece_read_timing_config_c(config_path, path_len, start_time, end_time, &
                                        timestep_seconds, max_len, rc) bind(C, name="cece_read_timing_config")
      use, intrinsic :: iso_c_binding
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value, intent(in) :: path_len
      character(kind=c_char), intent(out) :: start_time(*), end_time(*)
      integer(c_int), intent(out) :: timestep_seconds
      integer(c_int), value, intent(in) :: max_len
      integer(c_int), intent(out) :: rc
    end subroutine cece_read_timing_config_c
  end interface

contains

  !> @brief Set the driver config file path (called by mainApp)
  subroutine set_driver_config_file(config_file)
    character(len=*), intent(in) :: config_file
    g_driver_cfg_file = config_file
  end subroutine set_driver_config_file

  !> @brief Set the CECE YAML config file path (called by mainApp)
  subroutine set_cece_config_file(config_file)
    character(len=*), intent(in) :: config_file
    g_cece_yaml_file = config_file
  end subroutine set_cece_config_file

  !> @brief SetServices for the driver component
  subroutine SetServices(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    write(*,'(A)') "INFO: [Driver] SetServices entered"
    rc = ESMF_SUCCESS

    ! Derive from NUOPC_Driver
    write(*,'(A)') "INFO: [Driver] Calling NUOPC_CompDerive..."
    call flush(6)
    call NUOPC_CompDerive(driver, driverSS, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, &
      file=__FILE__)) &
      return  ! bail out

    ! Specialize driver
    call NUOPC_CompSpecialize(driver, specLabel=driver_label_SetModelServices, &
      specRoutine=SetModelServices, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, &
      file=__FILE__)) &
      return  ! bail out

    ! set driver verbosity
    call NUOPC_CompAttributeSet(driver, name="Verbosity", value="high", rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, &
      file=__FILE__)) &
      return  ! bail out

    write(*,'(A)') "INFO: [Driver] SetServices complete"
  end subroutine SetServices

  !> @brief SetModelServices - Configure the CECE model component
  subroutine SetModelServices(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    ! local variables (following NUOPC prototype pattern)
    type(ESMF_GridComp) :: child
    type(ESMF_Time) :: startTime
    type(ESMF_Time) :: stopTime
    type(ESMF_TimeInterval) :: timeStep
    type(ESMF_Clock) :: internalClock

    ! Variables for YAML timing configuration
    character(len=64) :: start_time_str, end_time_str
    integer :: timestep_sec, c_rc, c_path_len
    character(len=:), allocatable :: c_yaml_path

    rc = ESMF_SUCCESS

    write(*,'(A)') "INFO: [Driver] SetModelServices entered - following NUOPC prototype pattern"

    ! Set CECE config path for the cap (this is still needed)
    write(*,'(A,A)') "INFO: [Driver] Setting CECE config path: ", trim(g_cece_yaml_file)
    call CECE_SetConfigPath(trim(g_cece_yaml_file), rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: Failed to set CECE config path rc=", rc
      return
    end if

    ! SetServices for CECE component (following SingleModelOpenMPProto pattern)
    ! Note: Grid creation now handled by CECE component itself in InitializeRealize
    call NUOPC_DriverAddComp(driver, "CECE", ceceSS, comp=child, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: NUOPC_DriverAddComp failed rc=", rc
      return
    end if
    write(*,'(A)') "INFO: [Driver] CECE component added successfully (grid will be created by component)"

    ! Get timing configuration from YAML config file
    ! Convert Fortran string to C string
    c_yaml_path = trim(g_cece_yaml_file) // c_null_char
    c_path_len = len_trim(g_cece_yaml_file)

    ! Read timing from YAML
    call cece_read_timing_config_c(c_yaml_path, c_path_len, start_time_str, end_time_str, &
                                   timestep_sec, 64, c_rc)

    if (c_rc == 0) then
      ! Use configured timing from YAML
      call ESMF_TimeSet(startTime, timeString=trim(start_time_str), rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: Failed to set start time from YAML rc=", rc
        return
      end if

      call ESMF_TimeSet(stopTime, timeString=trim(end_time_str), rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: Failed to set stop time from YAML rc=", rc
        return
      end if

      call ESMF_TimeIntervalSet(timeStep, s=timestep_sec, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: Failed to set timestep from YAML rc=", rc
        return
      end if

      write(*,'(A,A,A,A,I0,A)') "INFO: [Driver] Using YAML timing: ", trim(start_time_str), &
                               " to ", trim(end_time_str), timestep_sec, " seconds"
    else
      ! Fallback to hardcoded values if YAML fails
      write(*,'(A)') "WARNING: [Driver] YAML timing config failed, using defaults"
      call ESMF_TimeSet(startTime, timeString="2020-01-01T00:00:00", rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: Failed to set default start time rc=", rc
        return
      end if

      call ESMF_TimeSet(stopTime, timeString="2020-01-01T06:00:00", rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: Failed to set default stop time rc=", rc
        return
      end if

      call ESMF_TimeIntervalSet(timeStep, s=3600, rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,'(A,I0)') "ERROR: Failed to set default timestep rc=", rc
        return
      end if
    end if
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: Failed to set timestep rc=", rc
      return
    end if

    ! Create internal clock
    internalClock = ESMF_ClockCreate(timeStep=timeStep, startTime=startTime, &
                                     stopTime=stopTime, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: ESMF_ClockCreate failed rc=", rc
      return
    end if

    ! Set the clock in the Driver
    call ESMF_GridCompSet(driver, clock=internalClock, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A,I0)') "ERROR: ESMF_GridCompSet failed rc=", rc
      return
    end if

    write(*,'(A)') "INFO: [Driver] Clock set successfully with YAML configuration"
    write(*,'(A)') "INFO: [Driver] SetModelServices complete - following NUOPC prototype pattern"

  end subroutine SetModelServices

  !> @brief Read driver configuration from ESMF config file
  !> @details Uses ESMF_Config to read simple key: value format
  subroutine read_driver_config(config_file, start_time, end_time, &
                                timestep_seconds, mesh_file, nx, ny, rc)
    character(len=*), intent(in) :: config_file
    character(len=*), intent(out) :: start_time, end_time, mesh_file
    integer, intent(out) :: timestep_seconds, nx, ny, rc

    type(ESMF_Config) :: config
    integer :: local_rc

    rc = ESMF_SUCCESS

    ! Initialize outputs to safe defaults before any reads
    start_time = "2020-01-01T00:00:00"
    end_time   = "2020-01-02T00:00:00"
    timestep_seconds = 3600
    mesh_file = ""
    nx = 4
    ny = 4

    ! Create and load ESMF config object
    config = ESMF_ConfigCreate(rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,'(A)') "WARNING: [Driver] Failed to create ESMF_Config, using defaults"
      start_time = "2020-01-01T00:00:00"
      end_time = "2020-01-02T00:00:00"
      timestep_seconds = 3600
      mesh_file = ""
      nx = 4
      ny = 4
      rc = ESMF_SUCCESS
      return
    end if

    ! Load the config file
    call ESMF_ConfigLoadFile(config, trim(config_file), rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,'(A,A)') "WARNING: [Driver] Failed to load config file: ", trim(config_file)
      ! Use defaults
      start_time = "2020-01-01T00:00:00"
      end_time = "2020-01-02T00:00:00"
      timestep_seconds = 3600
      mesh_file = ""
      nx = 4
      ny = 4
      call ESMF_ConfigDestroy(config, rc=local_rc)
      rc = ESMF_SUCCESS
      return
    end if

    ! Read start_time
    call ESMF_ConfigGetAttribute(config, start_time, label="start_time:", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      start_time = "2020-01-01T00:00:00"
    end if

    ! Read end_time
    call ESMF_ConfigGetAttribute(config, end_time, label="end_time:", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      end_time = "2020-01-02T00:00:00"
    end if

    ! Read timestep_seconds
    call ESMF_ConfigGetAttribute(config, timestep_seconds, label="timestep_seconds:", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      timestep_seconds = 3600
    end if

    ! Read grid_nx
    call ESMF_ConfigGetAttribute(config, nx, label="grid_nx:", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      nx = 4
    end if

    ! Read grid_ny
    call ESMF_ConfigGetAttribute(config, ny, label="grid_ny:", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      ny = 4
    end if

    ! Read mesh_file (optional)
    call ESMF_ConfigGetAttribute(config, mesh_file, label="mesh_file:", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      mesh_file = ""
    else
      ! Treat "none" or "NONE" as no mesh file
      if (trim(mesh_file) == "none" .or. trim(mesh_file) == "NONE") then
        mesh_file = ""
      end if
    end if

    ! Clean up
    call ESMF_ConfigDestroy(config, rc=local_rc)

  end subroutine read_driver_config

  !> @brief Parse ISO 8601 datetime string (YYYY-MM-DDTHH:MM:SS)
  !> @param iso_str Input ISO8601 datetime string
  !> @param time_out Output ESMF_Time object
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine parse_iso8601_to_esmf_time(iso_str, time_out, rc)
    character(len=*), intent(in) :: iso_str
    type(ESMF_Time), intent(out) :: time_out
    integer, intent(out) :: rc

    integer :: yy, mm, dd, hh, mn, ss
    integer :: str_len, iostat
    character(len=256) :: err_msg

    rc = ESMF_SUCCESS

    ! Validate string length (must be at least 19 characters for YYYY-MM-DDTHH:MM:SS)
    str_len = len_trim(iso_str)
    if (str_len < 19) then
      write(err_msg, '(A,I0,A)') "ERROR: [parse_iso8601] Invalid ISO8601 format - string too short (", &
                                 str_len, " chars, expected 19)"
      write(*,'(A)') trim(err_msg)
      rc = ESMF_FAILURE
      return
    end if

    ! Validate format separators
    if (iso_str(5:5) /= '-' .or. iso_str(8:8) /= '-' .or. &
        iso_str(11:11) /= 'T' .or. iso_str(14:14) /= ':' .or. &
        iso_str(17:17) /= ':') then
      write(*,'(A)') "ERROR: [parse_iso8601] Invalid ISO8601 format - incorrect separators"
      write(*,'(A,A)') "  Expected: YYYY-MM-DDTHH:MM:SS, got: ", trim(iso_str)
      rc = ESMF_FAILURE
      return
    end if

    ! Parse year
    read(iso_str(1:4), '(I4)', iostat=iostat) yy
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse year"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse month
    read(iso_str(6:7), '(I2)', iostat=iostat) mm
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse month"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse day
    read(iso_str(9:10), '(I2)', iostat=iostat) dd
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse day"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse hour
    read(iso_str(12:13), '(I2)', iostat=iostat) hh
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse hour"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse minute
    read(iso_str(15:16), '(I2)', iostat=iostat) mn
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse minute"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse second
    read(iso_str(18:19), '(I2)', iostat=iostat) ss
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse second"
      rc = ESMF_FAILURE
      return
    end if

    ! Validate ranges
    if (mm < 1 .or. mm > 12) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid month: ", mm
      rc = ESMF_FAILURE
      return
    end if

    if (dd < 1 .or. dd > 31) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid day: ", dd
      rc = ESMF_FAILURE
      return
    end if

    if (hh < 0 .or. hh > 23) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid hour: ", hh
      rc = ESMF_FAILURE
      return
    end if

    if (mn < 0 .or. mn > 59) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid minute: ", mn
      rc = ESMF_FAILURE
      return
    end if

    if (ss < 0 .or. ss > 59) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid second: ", ss
      rc = ESMF_FAILURE
      return
    end if

    ! Create ESMF_Time object
    call ESMF_TimeSet(time_out, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to create ESMF_Time"
      write(*,'(A,I4,A,I2,A,I2,A,I2,A,I2,A,I2)') &
        "  Date/time: ", yy, "-", mm, "-", dd, " ", hh, ":", mn, ":", ss
      return
    end if

  end subroutine parse_iso8601_to_esmf_time

  !> @brief Parse ISO 8601 datetime string to integer components (legacy interface)
  !> @param iso_str Input ISO8601 datetime string
  !> @param yy Year
  !> @param mm Month
  !> @param dd Day
  !> @param hh Hour
  !> @param mn Minute
  !> @param ss Second
  subroutine parse_iso8601(iso_str, yy, mm, dd, hh, mn, ss)
    character(len=*), intent(in) :: iso_str
    integer, intent(out) :: yy, mm, dd, hh, mn, ss

    type(ESMF_Time) :: temp_time
    integer :: rc

    ! Use the new validated parser
    call parse_iso8601_to_esmf_time(iso_str, temp_time, rc)
    if (rc /= ESMF_SUCCESS) then
      ! Fallback to simple parsing for backward compatibility
      read(iso_str(1:4), '(I4)') yy
      read(iso_str(6:7), '(I2)') mm
      read(iso_str(9:10), '(I2)') dd
      read(iso_str(12:13), '(I2)') hh
      read(iso_str(15:16), '(I2)') mn
      read(iso_str(18:19), '(I2)') ss
      return
    end if

    ! Extract components from ESMF_Time
    call ESMF_TimeGet(temp_time, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, rc=rc)
  end subroutine parse_iso8601

  !> @brief Format ESMF_Time to ISO8601 string
  !> @param time_in Input ESMF_Time object
  !> @param iso_str Output ISO8601 datetime string (must be at least 19 characters)
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine format_esmf_time_to_iso8601(time_in, iso_str, rc)
    type(ESMF_Time), intent(in) :: time_in
    character(len=*), intent(out) :: iso_str
    integer, intent(out) :: rc

    integer :: yy, mm, dd, hh, mn, ss

    rc = ESMF_SUCCESS

    ! Extract components from ESMF_Time
    call ESMF_TimeGet(time_in, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [format_esmf_time_to_iso8601] Failed to extract time components"
      return
    end if

    ! Format as ISO8601 string
    write(iso_str, '(I4.4,A,I2.2,A,I2.2,A,I2.2,A,I2.2,A,I2.2)') &
      yy, '-', mm, '-', dd, 'T', hh, ':', mn, ':', ss

  end subroutine format_esmf_time_to_iso8601

  !> @brief C wrapper for parse_iso8601_to_esmf_time (for testing)
  !> @param iso_str Input ISO8601 datetime string (null-terminated C string)
  !> @param yy Output year
  !> @param mm Output month
  !> @param dd Output day
  !> @param hh Output hour
  !> @param mn Output minute
  !> @param ss Output second
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine parse_iso8601_to_esmf_time_c_wrapper(iso_str, yy, mm, dd, hh, mn, ss, rc) &
    bind(C, name="parse_iso8601_to_esmf_time_c_wrapper")
    use, intrinsic :: iso_c_binding
    character(kind=c_char), dimension(*), intent(in) :: iso_str
    integer(c_int), intent(out) :: yy, mm, dd, hh, mn, ss, rc

    type(ESMF_Time) :: time_out
    character(len=256) :: fortran_str
    integer :: i, str_len

    ! Convert C string to Fortran string
    str_len = 0
    do i = 1, 256
      if (iso_str(i) == c_null_char) exit
      fortran_str(i:i) = iso_str(i)
      str_len = i
    end do

    ! Parse ISO8601 string
    call parse_iso8601_to_esmf_time(fortran_str(1:str_len), time_out, rc)
    if (rc /= ESMF_SUCCESS) then
      return
    end if

    ! Extract components from ESMF_Time
    call ESMF_TimeGet(time_out, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, rc=rc)

  end subroutine parse_iso8601_to_esmf_time_c_wrapper

  !> @brief C wrapper for format_esmf_time_to_iso8601 (for testing)
  !> @param yy Input year
  !> @param mm Input month
  !> @param dd Input day
  !> @param hh Input hour
  !> @param mn Input minute
  !> @param ss Input second
  !> @param iso_str Output ISO8601 datetime string (null-terminated C string)
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine format_esmf_time_to_iso8601_c_wrapper(yy, mm, dd, hh, mn, ss, iso_str, rc) &
    bind(C, name="format_esmf_time_to_iso8601_c_wrapper")
    use, intrinsic :: iso_c_binding
    integer(c_int), intent(in) :: yy, mm, dd, hh, mn, ss
    character(kind=c_char), dimension(*), intent(out) :: iso_str
    integer(c_int), intent(out) :: rc

    type(ESMF_Time) :: time_in
    character(len=256) :: fortran_str
    integer :: i

    ! Create ESMF_Time from components
    call ESMF_TimeSet(time_in, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [format_esmf_time_to_iso8601_c_wrapper] Failed to create ESMF_Time"
      return
    end if

    ! Format to ISO8601 string
    call format_esmf_time_to_iso8601(time_in, fortran_str, rc)
    if (rc /= ESMF_SUCCESS) then
      return
    end if

    ! Convert Fortran string to C string (null-terminated)
    do i = 1, len_trim(fortran_str)
      iso_str(i) = fortran_str(i:i)
    end do
    iso_str(len_trim(fortran_str) + 1) = c_null_char

  end subroutine format_esmf_time_to_iso8601_c_wrapper

  !> @brief Set grid coordinates
  subroutine set_grid_coordinates(grid, nx, ny, rc)
    type(ESMF_Grid), intent(inout) :: grid
    integer, intent(in) :: nx, ny
    integer, intent(out) :: rc

    real(ESMF_KIND_R8), pointer :: grid_lon(:,:), grid_lat(:,:)
    real(ESMF_KIND_R8) :: dlon, dlat
    integer :: i, j

    rc = ESMF_SUCCESS

    dlon = 360.0d0 / real(nx, ESMF_KIND_R8)
    dlat = 180.0d0 / real(ny, ESMF_KIND_R8)

    call ESMF_GridGetCoord(grid, coordDim=1, localDE=0, &
                          staggerloc=ESMF_STAGGERLOC_CENTER, &
                          farrayPtr=grid_lon, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    call ESMF_GridGetCoord(grid, coordDim=2, localDE=0, &
                          staggerloc=ESMF_STAGGERLOC_CENTER, &
                          farrayPtr=grid_lat, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    do j = 1, ny
      do i = 1, nx
        grid_lon(i,j) = (real(i, ESMF_KIND_R8) - 0.5d0) * dlon - 180.0d0
        grid_lat(i,j) = (real(j, ESMF_KIND_R8) - 0.5d0) * dlat - 90.0d0
      end do
    end do
  end subroutine set_grid_coordinates

  !> @brief Check if timestep divides evenly into simulation duration
  !> @details Logs a warning if the timestep doesn't divide evenly into the
  !>          total simulation duration (Requirement 3.4, 14.7).
  !> @param startTime Simulation start time
  !> @param stopTime Simulation stop time
  !> @param timeStep Timestep interval
  !> @param rc Return code (always ESMF_SUCCESS, warnings only)
  subroutine check_timestep_divisibility(startTime, stopTime, timeStep, rc)
    type(ESMF_Time), intent(in) :: startTime, stopTime
    type(ESMF_TimeInterval), intent(in) :: timeStep
    integer, intent(out) :: rc

    type(ESMF_TimeInterval) :: duration, remainder
    integer :: duration_secs, timestep_secs, num_steps
    real(ESMF_KIND_R8) :: exact_steps

    rc = ESMF_SUCCESS

    ! Calculate total duration
    duration = stopTime - startTime

    ! Get duration and timestep in seconds
    call ESMF_TimeIntervalGet(duration, s=duration_secs, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "WARNING: [Driver] Failed to get duration in seconds"
      rc = ESMF_SUCCESS  ! Don't fail on warning
      return
    end if

    call ESMF_TimeIntervalGet(timeStep, s=timestep_secs, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "WARNING: [Driver] Failed to get timestep in seconds"
      rc = ESMF_SUCCESS  ! Don't fail on warning
      return
    end if

    ! Check if timestep divides evenly into duration
    num_steps = duration_secs / timestep_secs
    exact_steps = real(duration_secs, ESMF_KIND_R8) / real(timestep_secs, ESMF_KIND_R8)

    if (abs(exact_steps - real(num_steps, ESMF_KIND_R8)) > 1.0e-6_ESMF_KIND_R8) then
      write(*,'(A)') "WARNING: [Driver] Timestep does not divide evenly into simulation duration"
      write(*,'(A,I0,A)') "  Duration: ", duration_secs, " seconds"
      write(*,'(A,I0,A)') "  Timestep: ", timestep_secs, " seconds"
      write(*,'(A,F12.6)') "  Exact number of steps: ", exact_steps
      write(*,'(A,I0)') "  Integer number of steps: ", num_steps
      write(*,'(A)') "  The simulation may not reach the exact end time."
    end if

  end subroutine check_timestep_divisibility

  !> @brief Log an error message and abort execution.
  !>
  !> Logs a fatal error message with error code and operation name,
  !> ensures ESMF_Finalize is called, and exits with non-zero status.
  !>
  !> Requirements: 18.1, 18.2, 18.3
  !>
  !> @param message Error message to log
  !> @param error_code ESMF error code
  subroutine driver_abort(message, error_code)
    character(len=*), intent(in) :: message
    integer, intent(in), optional :: error_code

    integer :: rc

    ! Log error message
    if (present(error_code)) then
      write(*,'(A,A,A,I0)') "ERROR: [Driver] ", trim(message), " (rc=", error_code, ")"
    else
      write(*,'(A,A)') "ERROR: [Driver] ", trim(message)
    end if

    ! Ensure ESMF is finalized even on error
    call ESMF_Finalize(rc=rc)

    ! Exit with non-zero status
    stop 1

  end subroutine driver_abort

  !> @brief Log a message with specified level.
  !>
  !> Logs informational, warning, or error messages with consistent formatting.
  !> Includes error codes and operation names when provided.
  !>
  !> Requirements: 18.1, 18.2, 18.5
  !>
  !> @param level Log level: "INFO", "WARNING", or "ERROR"
  !> @param message Message to log
  !> @param error_code Optional ESMF error code
  subroutine driver_log(level, message, error_code)
    character(len=*), intent(in) :: level, message
    integer, intent(in), optional :: error_code

    if (present(error_code)) then
      write(*,'(A,A,A,A,A,I0)') trim(level), ": [Driver] ", trim(message), " (rc=", error_code, ")"
    else
      write(*,'(A,A,A)') trim(level), ": [Driver] ", trim(message)
    end if

  end subroutine driver_log

  !> @brief Perform grid-size-dependent synchronization.
  !>
  !> Determines the number of VM barriers based on grid size:
  !> - ≤50,000 points: 1 barrier
  !> - 50,001-100,000 points: 2 barriers
  !> - 100,001-500,000 points: 3 barriers
  !> - >500,000 points: 4 barriers
  !>
  !> Requirements: 12.1, 12.2, 12.3, 12.4, 12.5
  !>
  !> @param vm ESMF Virtual Machine
  !> @param grid_size Total grid size (nx * ny)
  !> @param rc Return code (ESMF_SUCCESS on success)
  subroutine perform_grid_size_synchronization(vm, grid_size, rc)
    type(ESMF_VM), intent(in) :: vm
    integer, intent(in) :: grid_size
    integer, intent(out) :: rc

    integer :: sync_level, i, barrier_rc

    rc = ESMF_SUCCESS

    ! Determine synchronization level based on grid size
    if (grid_size <= 50000) then
      sync_level = 1
    else if (grid_size <= 100000) then
      sync_level = 2
    else if (grid_size <= 500000) then
      sync_level = 3
    else
      sync_level = 4
    end if

    write(*,'(A,I0,A,I0)') "INFO: [Driver] Grid size: ", grid_size, " points, sync level: ", sync_level

    ! Call VM barriers
    do i = 1, sync_level
      call ESMF_VMBarrier(vm, rc=barrier_rc)
      if (barrier_rc /= ESMF_SUCCESS) then
        call driver_log("WARNING", "VM barrier failed", barrier_rc)
        ! Don't fail on barrier errors - continue cleanup
      end if
    end do

  end subroutine perform_grid_size_synchronization

  !> @brief Perform resource cleanup sequence.
  !>
  !> Destroys ESMF resources in the proper order:
  !> 1. Import state
  !> 2. Export state
  !> 3. Mesh
  !> 4. Grid
  !> 5. Clock
  !> 6. Calendar
  !> 7. GridComp
  !>
  !> Requirements: 11.1, 11.2, 11.3
  !>
  !> @param driver Driver GridComp
  !> @param ceceComp CECE GridComp
  !> @param importState Import state
  !> @param exportState Export state
  !> @param grid ESMF Grid
  !> @param mesh ESMF Mesh
  !> @param clock ESMF Clock
  !> @param rc Return code (ESMF_SUCCESS on success)
  subroutine cleanup_resources(driver, ceceComp, importState, exportState, grid, mesh, clock, rc)
    type(ESMF_GridComp), intent(inout) :: driver, ceceComp
    type(ESMF_State), intent(inout) :: importState, exportState
    type(ESMF_Grid), intent(inout) :: grid
    type(ESMF_Mesh), intent(inout) :: mesh
    type(ESMF_Clock), intent(inout) :: clock
    integer, intent(out) :: rc

    integer :: cleanup_rc

    rc = ESMF_SUCCESS

    call driver_log("INFO", "=== Phase: Resource Cleanup ===")

    ! 1. Destroy import state
    call driver_log("INFO", "Destroying import state...")
    call ESMF_StateDestroy(importState, rc=cleanup_rc)
    if (cleanup_rc /= ESMF_SUCCESS) then
      call driver_log("WARNING", "Failed to destroy import state", cleanup_rc)
    end if

    ! 2. Destroy export state
    call driver_log("INFO", "Destroying export state...")
    call ESMF_StateDestroy(exportState, rc=cleanup_rc)
    if (cleanup_rc /= ESMF_SUCCESS) then
      call driver_log("WARNING", "Failed to destroy export state", cleanup_rc)
    end if

    ! 3. Destroy mesh
    call driver_log("INFO", "Destroying mesh...")
    call ESMF_MeshDestroy(mesh, rc=cleanup_rc)
    if (cleanup_rc /= ESMF_SUCCESS) then
      call driver_log("WARNING", "Failed to destroy mesh", cleanup_rc)
    end if

    ! 4. Destroy grid
    call driver_log("INFO", "Destroying grid...")
    call ESMF_GridDestroy(grid, rc=cleanup_rc)
    if (cleanup_rc /= ESMF_SUCCESS) then
      call driver_log("WARNING", "Failed to destroy grid", cleanup_rc)
    end if

    ! 5. Destroy clock
    call driver_log("INFO", "Destroying clock...")
    call ESMF_ClockDestroy(clock, rc=cleanup_rc)
    if (cleanup_rc /= ESMF_SUCCESS) then
      call driver_log("WARNING", "Failed to destroy clock", cleanup_rc)
    end if

    ! 6. Destroy calendar (implicit in clock destruction, but explicit for clarity)
    call driver_log("INFO", "Calendar destroyed with clock")

    ! 7. Destroy GridComp
    call driver_log("INFO", "Destroying CECE GridComp...")
    call ESMF_GridCompDestroy(ceceComp, rc=cleanup_rc)
    if (cleanup_rc /= ESMF_SUCCESS) then
      call driver_log("WARNING", "Failed to destroy CECE GridComp", cleanup_rc)
    end if

    call driver_log("INFO", "Resource cleanup complete")

  end subroutine cleanup_resources

  !> @brief Implement MPI synchronization for multi-process execution.
  !>
  !> Task 11.3: Implement MPI synchronization
  !> Calls ESMF_VMBarrier before and after Run phase to ensure all processes
  !> complete before proceeding. This is essential for multi-process execution.
  !>
  !> Requirements: 9.4
  !>
  !> @param vm ESMF Virtual Machine
  !> @param phase_name Name of the phase (for logging)
  !> @param rc Return code (ESMF_SUCCESS on success)
  subroutine mpi_synchronize(vm, phase_name, rc)
    type(ESMF_VM), intent(in) :: vm
    character(len=*), intent(in) :: phase_name
    integer, intent(out) :: rc

    integer :: petCount, barrier_rc

    rc = ESMF_SUCCESS

    ! Get process count
    call ESMF_VMGet(vm, petCount=petCount, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      call driver_log("WARNING", "Failed to get VM process count", rc)
      return
    end if

    ! Only synchronize in multi-process mode
    if (petCount > 1) then
      call ESMF_VMBarrier(vm, rc=barrier_rc)
      if (barrier_rc /= ESMF_SUCCESS) then
        call driver_log("WARNING", "VM barrier failed during " // trim(phase_name), barrier_rc)
        ! Don't fail on barrier errors - continue execution
      end if
    end if

  end subroutine mpi_synchronize

end module driver
