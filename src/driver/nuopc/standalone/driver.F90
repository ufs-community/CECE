!> @file driver.F90
!> @brief NUOPC Driver component for CECE standalone execution.
!>
!> Specializes NUOPC_Driver to manage a single CECE model component.
!> Handles clock management, run loop, and component lifecycle.

module driver

  use ESMF
  use NUOPC
  use NUOPC_Driver, driverSS => SetServices
  use NUOPC_Driver, only: driver_label_SetModelServices => label_SetModelServices
  use cece_cap_mod, ceceSS => CECE_SetServices
  use, intrinsic :: iso_c_binding

  implicit none

  private

  public SetServices, set_cece_config_file

  !> @brief CECE config file path (.yaml format, for cap/physics/streams)
  character(len=512), save :: g_cece_yaml_file = "cece_config.yaml"

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

  !> @brief Set the CECE YAML config file path (called by mainApp)
  subroutine set_cece_config_file(config_file)
    character(len=*), intent(in) :: config_file
    g_cece_yaml_file = config_file
  end subroutine set_cece_config_file

  !> @brief SetServices for the driver component
  subroutine SetServices(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    rc = ESMF_SUCCESS

    call ESMF_LogWrite('[Driver] SetServices entered', ESMF_LOGMSG_INFO)

    ! Derive from NUOPC_Driver
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

    call ESMF_LogWrite('[Driver] SetServices complete', ESMF_LOGMSG_INFO)
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
    character(len=700) :: wmsg
    integer :: timestep_sec, c_rc, c_path_len
    character(len=:), allocatable :: c_yaml_path

    rc = ESMF_SUCCESS

    call ESMF_LogWrite('[Driver] SetModelServices entered', ESMF_LOGMSG_INFO)

    ! Set CECE config path for the cap (this is still needed)
    write(wmsg, '(A,A)') '[Driver] Setting CECE config path: ', trim(g_cece_yaml_file)
    call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_INFO)
    call CECE_SetConfigPath(trim(g_cece_yaml_file), rc)
    if (rc /= ESMF_SUCCESS) then
      write(wmsg, '(A,I0)') '[Driver] Failed to set CECE config path rc=', rc
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    ! SetServices for CECE component (following SingleModelOpenMPProto pattern)
    ! Note: Grid creation now handled by CECE component itself in InitializeRealize
    call NUOPC_DriverAddComp(driver, "CECE", ceceSS, comp=child, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out
    call ESMF_LogWrite('[Driver] CECE component added successfully'// &
      ' (grid will be created by component)', ESMF_LOGMSG_INFO)

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
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      call ESMF_TimeSet(stopTime, timeString=trim(end_time_str), rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      call ESMF_TimeIntervalSet(timeStep, s=timestep_sec, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      write(wmsg, '(A,A,A,A,I0,A)') '[Driver] Using YAML timing: ', &
        trim(start_time_str), ' to ', trim(end_time_str), timestep_sec, ' seconds'
      call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_INFO)
    else
      ! The standalone C++ driver requires 'driver.start_time' and
      ! 'driver.end_time' and aborts when either is missing, so the cap must
      ! not quietly substitute defaults here -- doing so would let a config
      ! run with a different clock on the two drivers. Fail loudly instead.
      write(wmsg, '(A,A,A)') '[Driver] Failed to read timing from YAML ', &
        trim(g_cece_yaml_file), ' (driver.start_time/driver.end_time required)'
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    ! Create internal clock
    internalClock = ESMF_ClockCreate(timeStep=timeStep, startTime=startTime, &
                                     stopTime=stopTime, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Set the clock in the Driver
    call ESMF_GridCompSet(driver, clock=internalClock, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_LogWrite('[Driver] Clock set successfully with YAML configuration', &
      ESMF_LOGMSG_INFO)
    call ESMF_LogWrite('[Driver] SetModelServices complete', ESMF_LOGMSG_INFO)

  end subroutine SetModelServices

end module driver
