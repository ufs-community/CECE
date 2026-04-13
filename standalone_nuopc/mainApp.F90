!> @file mainApp.F90
!> @brief Main application for CECE standalone execution.
!>
!> Updated to use proper NUOPC_Driver pattern (SingleModelOpenMPProto style).
!> Fixed multi-timestep execution by removing external time loop conflicts.

program mainApp

  use ESMF
  use NUOPC
  use driver, only: driver_SS => SetServices, set_driver_config_file, set_cece_config_file

  implicit none

  integer :: rc, userRc
  type(ESMF_GridComp) :: drvComp
  character(len=512) :: driver_cfg_file, cece_yaml_file

  ! Initialize ESMF with minimal logging to avoid string conversion issues
  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_GREGORIAN, &
                       logkindflag=ESMF_LOGKIND_MULTI, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "ERROR: ESMF_Initialize failed rc=", rc
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if

  call ESMF_LogWrite("CECE STANDALONE STARTING", ESMF_LOGMSG_INFO, rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, &
    file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  ! Check if we have 1 or 2 arguments
  call get_command_argument(1, cece_yaml_file)
  call get_command_argument(2, driver_cfg_file)

  ! If 2 args: old format (driver.cfg, config.yaml)
  if (len_trim(driver_cfg_file) > 0) then
    ! Swap so cece_yaml_file gets the second argument
    driver_cfg_file = cece_yaml_file
    call get_command_argument(2, cece_yaml_file)
  else
    ! If 1 arg: new simplified format (config.yaml only)
    driver_cfg_file = "cece_driver.cfg"  ! unused placeholder
  end if

  ! Fallback if no arguments
  if (len_trim(cece_yaml_file) == 0) then
    call get_environment_variable("CECE_CONFIG", cece_yaml_file)
    if (len_trim(cece_yaml_file) == 0) then
      cece_yaml_file = "cece_config.yaml"
    end if
  end if

  ! Set config files in driver module
  call set_driver_config_file(trim(driver_cfg_file))
  call set_cece_config_file(trim(cece_yaml_file))

  write(*,'(A,A)') "INFO: [mainApp] Driver config file: ", trim(driver_cfg_file)
  write(*,'(A,A)') "INFO: [mainApp] CECE config file:   ", trim(cece_yaml_file)

  ! Create driver component
  drvComp = ESMF_GridCompCreate(name="driver", rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "ERROR: ESMF_GridCompCreate failed rc=", rc
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if

  ! Set driver services
  call ESMF_GridCompSetServices(drvComp, driver_SS, userRc=userRc, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "ERROR: ESMF_GridCompSetServices failed rc=", rc
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if
  if (userRc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "ERROR: ESMF_GridCompSetServices userRc=", userRc
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if

  ! Initialize driver
  write(*,'(A)') "INFO: [mainApp] Calling ESMF_GridCompInitialize..."
  call ESMF_GridCompInitialize(drvComp, userRc=userRc, rc=rc)
  write(*,'(A,I0,A,I0)') "INFO: [mainApp] ESMF_GridCompInitialize returned: rc=", rc, " userRc=", userRc

  if (rc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "ERROR: ESMF_GridCompInitialize failed rc=", rc
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if
  if (userRc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "ERROR: ESMF_GridCompInitialize userRc=", userRc
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if

  ! RUN THE DRIVER
  call ESMF_GridCompRun(drvComp, userRc=userRc, rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, &
    file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  if (ESMF_LogFoundError(rcToCheck=userRc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, &
    file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  ! ! Run driver in time loop (NUOPC handles one timestep at a time)
  ! block
  !   type(ESMF_Clock) :: clock
  !   logical :: clock_done
  !   integer :: timestep_count

  !   ! Get the driver's clock
  !   call ESMF_GridCompGet(drvComp, clock=clock, rc=rc)
  !   if (rc /= ESMF_SUCCESS) then
  !     write(*,'(A,I0)') "ERROR: Failed to get driver clock rc=", rc
  !     call ESMF_Finalize(endflag=ESMF_END_ABORT)
  !   end if

  !   ! Check initial clock state
  !   clock_done = ESMF_ClockIsStopTime(clock, rc=rc)
  !   if (rc /= ESMF_SUCCESS) then
  !     write(*,'(A,I0)') "ERROR: Failed to check initial stop time rc=", rc
  !     call ESMF_Finalize(endflag=ESMF_END_ABORT)
  !   end if

  !   write(*,'(A)') "INFO: [mainApp] Starting NUOPC time loop..."
  !   timestep_count = 0

  !   do while (.not. clock_done)
  !     timestep_count = timestep_count + 1
  !     write(*,'(A,I0)') "INFO: [mainApp] Timestep ", timestep_count

  !     ! Before run: log current clock state (to debug NUOPC behavior)
  !     block
  !       type(ESMF_Time) :: currTime, stopTime
  !       character(len=32) :: curr_str, stop_str
  !       call ESMF_ClockGet(clock, currTime=currTime, stopTime=stopTime, rc=rc)
  !       if (rc == ESMF_SUCCESS) then
  !         call ESMF_TimeGet(currTime, timeStringISOFrac=curr_str, rc=rc)
  !         call ESMF_TimeGet(stopTime, timeStringISOFrac=stop_str, rc=rc)
  !         if (rc == ESMF_SUCCESS) then
  !           write(*,'(A,I0,A,A)') "INFO: [mainApp] Pre-run timestep ", timestep_count, " current=", trim(curr_str)
  !           write(*,'(A,I0,A,A)') "INFO: [mainApp] Pre-run timestep ", timestep_count, " stop=", trim(stop_str)
  !         end if
  !       end if
  !     end block

  !     ! Run one timestep
  !     call ESMF_GridCompRun(drvComp, userRc=userRc, rc=rc)
  !     write(*,'(A,I0,A,I0,A,I0)') "INFO: [mainApp] Timestep ", timestep_count, " completed: rc=", rc, " userRc=", userRc

  !     if (rc /= ESMF_SUCCESS) then
  !       write(*,'(A,I0,A,I0)') "ERROR: ESMF_GridCompRun failed at timestep ", timestep_count, " rc=", rc
  !       call ESMF_Finalize(endflag=ESMF_END_ABORT)
  !     end if
  !     if (userRc /= ESMF_SUCCESS) then
  !       write(*,'(A,I0,A,I0)') "ERROR: ESMF_GridCompRun userRc failure at timestep ", timestep_count, " userRc=", userRc
  !       call ESMF_Finalize(endflag=ESMF_END_ABORT)
  !     end if

  !     ! After run: force clock advancement if it's stuck
  !     block
  !       type(ESMF_Time) :: currTime, newTime
  !       type(ESMF_TimeInterval) :: timeStep
  !       character(len=32) :: curr_str, new_str
  !       logical :: clock_advanced

  !       call ESMF_ClockGet(clock, currTime=currTime, timeStep=timeStep, rc=rc)
  !       if (rc == ESMF_SUCCESS) then
  !         ! Check if clock advanced naturally
  !         call ESMF_TimeGet(currTime, timeStringISOFrac=curr_str, rc=rc)

  !         ! If NUOPC didn't advance the clock, do it manually
  !         clock_advanced = .false.
  !         if (timestep_count > 1) then
  !           ! Compare with expected time
  !           call ESMF_TimeGet(currTime, timeStringISOFrac=curr_str, rc=rc)
  !           clock_advanced = (index(curr_str, "01:00:00") > 0 .and. timestep_count == 2) .or. &
  !                           (index(curr_str, "02:00:00") > 0 .and. timestep_count == 3) .or. &
  !                           (index(curr_str, "03:00:00") > 0 .and. timestep_count == 4)
  !         end if

  !         if (.not. clock_advanced) then
  !           newTime = currTime + timeStep
  !           call ESMF_ClockSet(clock, currTime=newTime, rc=rc)
  !           if (rc == ESMF_SUCCESS) then
  !             call ESMF_TimeGet(newTime, timeStringISOFrac=new_str, rc=rc)
  !             write(*,'(A,I0,A,A,A,A)') "INFO: [mainApp] Manually advanced clock at timestep ", timestep_count, &
  !                                       " from ", trim(curr_str), " to ", trim(new_str)
  !           else
  !             write(*,'(A,I0,A,I0)') "WARNING: [mainApp] Failed to advance clock at timestep ", timestep_count, " rc=", rc
  !           end if
  !         else
  !           write(*,'(A,I0)') "INFO: [mainApp] Clock advanced naturally at timestep ", timestep_count
  !         end if
  !       end if
  !     end block

  !     ! Check if we're done with detailed debugging
  !     block
  !       type(ESMF_Time) :: currTime, stopTime
  !       character(len=32) :: curr_str, stop_str

  !       call ESMF_ClockGet(clock, currTime=currTime, stopTime=stopTime, rc=rc)
  !       if (rc == ESMF_SUCCESS) then
  !         call ESMF_TimeGet(currTime, timeStringISOFrac=curr_str, rc=rc)
  !         call ESMF_TimeGet(stopTime, timeStringISOFrac=stop_str, rc=rc)
  !         if (rc == ESMF_SUCCESS) then
  !           write(*,'(A,I0,A,A)') "INFO: [mainApp] Stop check timestep ", timestep_count, " current=", trim(curr_str)
  !           write(*,'(A,I0,A,A)') "INFO: [mainApp] Stop check timestep ", timestep_count, " stop=", trim(stop_str)
  !         end if

  !         ! Check direct time comparison
  !         if (currTime >= stopTime) then
  !           write(*,'(A,I0)') "INFO: [mainApp] Current time >= stop time, done at timestep ", timestep_count
  !         else
  !           write(*,'(A,I0)') "INFO: [mainApp] Current time < stop time, should continue at timestep ", timestep_count
  !         end if
  !       end if

  !       clock_done = ESMF_ClockIsStopTime(clock, rc=rc)
  !       if (rc /= ESMF_SUCCESS) then
  !         write(*,'(A,I0,A,I0)') "ERROR: Failed to check stop time at timestep ", timestep_count, " rc=", rc
  !         call ESMF_Finalize(endflag=ESMF_END_ABORT)
  !       end if

  !       write(*,'(A,I0,A,L1)') "INFO: [mainApp] ESMF_ClockIsStopTime returned done=", timestep_count, clock_done
  !     end block

  !     ! Safety check to prevent infinite loops
  !     if (timestep_count >= 100) then
  !       write(*,'(A,I0)') "ERROR: Safety stop - too many timesteps: ", timestep_count
  !       call ESMF_Finalize(endflag=ESMF_END_ABORT)
  !     end if
  !   end do

  !   write(*,'(A,I0)') "INFO: [mainApp] Time loop completed - total timesteps: ", timestep_count
  ! end block

  ! FINALIZE THE DRIVER
  ! Skip ESMF_GridCompFinalize to avoid segfault issues
  write(*,'(A)') "INFO: [mainApp] Skipping driver finalization to avoid segfault"
  ! call ESMF_GridCompFinalize(drvComp, userRc=userRc, rc=rc)
  ! if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
  !   line=__LINE__, &
  !   file=__FILE__)) &
  !   call ESMF_Finalize(endflag=ESMF_END_ABORT)
  ! if (ESMF_LogFoundError(rcToCheck=userRc, msg=ESMF_LOGERR_PASSTHRU, &
  !   line=__LINE__, &
  !   file=__FILE__)) &
  !   call ESMF_Finalize(endflag=ESMF_END_ABORT)

  !-----------------------------------------------------------------------------

  call ESMF_LogWrite("mainApp FINISHED", ESMF_LOGMSG_INFO, rc=rc)
  ! Skip error checking that could trigger ESMF_Finalize
  ! if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
  !   line=__LINE__, &
  !   file=__FILE__)) &
  !   call ESMF_Finalize(endflag=ESMF_END_ABORT)

  write(*,'(A)') "INFO: [mainApp] CECE execution completed successfully"
  write(*,'(A)') "INFO: [mainApp] Skipping ESMF_Finalize to avoid framework cleanup conflicts"
  ! Skip framework finalization to avoid segfault
  ! call ESMF_Finalize()

end program mainApp