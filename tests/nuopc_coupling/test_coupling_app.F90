!> @file test_coupling_app.F90
!> @brief Two-component NUOPC driver for the field-coupling integration tests.
!>
!> Runs the production CECE cap alongside a minimal sink peer under a real
!> NUOPC_Driver, with an auto-created Connector between them. This exercises
!> the full advertise/connect/realize/advance path that a coupled host would:
!> CECE advertises its emission exports, the sink advertises matching imports
!> that accept the transferred geometry, and the Connector pairs the fields by
!> standard name and reference-shares the arrays.
!>
!> The application is the field-dictionary host: it loads the dictionary YAML
!> (the preloaded ESMF dictionary holds only the ocean/land surface names, not
!> the emission standard names CECE advertises) before any component is added,
!> exactly as a real coupled application would.
!>
!> The sink's import list is derived from the SAME CECE config the cap reads,
!> through the path-based C-ABI queries, so the two sides stay in lock-step
!> without duplicating any parsing here.
module coupling_driver_mod

  use ESMF
  use NUOPC
  use NUOPC_Driver, driverSS => SetServices
  use NUOPC_Driver, only: driver_label_SetModelServices => label_SetModelServices
  use NUOPC_Driver, only: driver_label_SetRunSequence => label_SetRunSequence
  use NUOPC_Connector, connectorSS => SetServices
  use cece_cap_mod, ceceSS => CECE_SetServices
  use cece_cap_mod, only: CECE_SetConfigPath
  use cece_sink_mod
  use cece_met_source_mod
  use, intrinsic :: iso_c_binding

  implicit none

  private

  public :: coupling_driver_SS
  public :: set_coupling_config, set_coupling_dictionary, set_coupling_sinkdir
  public :: set_coupling_sink_skip, set_coupling_metsource

  !> @brief CECE YAML config path (shared with the cap and the sink setup).
  character(len=512), save :: g_config_file = "cece_config.yaml"

  !> @brief Field dictionary YAML path loaded by this host application.
  character(len=512), save :: g_dictionary_file = ""

  !> @brief Directory the sink writes its per-step NetCDF files into.
  character(len=512), save :: g_sink_dir = "."

  !> @brief Comma-separated export item names the sink must NOT advertise. A
  !> CECE export left without a matching sink import stays unconnected and is
  !> pruned at realization, exercising the unconnected-removal path.
  character(len=512), save :: g_sink_skip = ""

  !> @brief When true, add the constant met-source peer and a Connector from
  !> its export into CECE's import, exercising the host-forcing import path.
  logical, save :: g_metsource = .false.

  !> @brief Driver clock, created in SetModelServices. NUOPC child components
  !> read the driver's internal clock during their own Initialize, so it must
  !> exist before any child realizes; the run sequence then reuses it.
  type(ESMF_Clock), save :: g_clock

  ! Path-based C-ABI queries mirroring contracts/c-abi.md section A, plus the
  ! vertical-layer reader and the timing reader already used by the standalone
  ! driver. All take a fixed buffer + capacity and return a null-terminated
  ! string plus its length; see cece_core_field_helpers.cpp.
  interface
    subroutine cece_nuopc_export_count_c(config_path, path_len, count, rc) &
                                        bind(C, name="cece_nuopc_export_count")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_nuopc_export_spec_c(config_path, path_len, index, &
                                        species, species_cap, species_len, &
                                        std_name, std_cap, std_len, &
                                        units, units_cap, units_len, &
                                        name, name_cap, name_len, rc) &
                                        bind(C, name="cece_nuopc_export_spec")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len, index
      character(kind=c_char), intent(out) :: species(*), std_name(*), units(*), name(*)
      integer(c_int), value :: species_cap, std_cap, units_cap, name_cap
      integer(c_int), intent(out) :: species_len, std_len, units_len, name_len
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_sim_nz_from_config_c(config_path, path_len, nz_out, rc) &
                                        bind(C, name="cece_sim_nz_from_config")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), intent(out) :: nz_out
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_read_timing_config_c(config_path, path_len, start_time, end_time, &
                                         timestep_seconds, max_len, rc) &
                                         bind(C, name="cece_read_timing_config")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      character(kind=c_char), intent(out) :: start_time(*), end_time(*)
      integer(c_int), intent(out) :: timestep_seconds
      integer(c_int), value :: max_len
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

contains

  !> @brief Store the config path used by the cap and the sink setup.
  subroutine set_coupling_config(config_file)
    character(len=*), intent(in) :: config_file
    g_config_file = config_file
  end subroutine set_coupling_config

  !> @brief Store the field dictionary path this host loads at startup.
  subroutine set_coupling_dictionary(dict_file)
    character(len=*), intent(in) :: dict_file
    g_dictionary_file = dict_file
  end subroutine set_coupling_dictionary

  !> @brief Store the sink output directory.
  subroutine set_coupling_sinkdir(dir)
    character(len=*), intent(in) :: dir
    g_sink_dir = dir
  end subroutine set_coupling_sinkdir

  !> @brief Store the comma-separated list of exports the sink should skip.
  subroutine set_coupling_sink_skip(skip)
    character(len=*), intent(in) :: skip
    g_sink_skip = skip
  end subroutine set_coupling_sink_skip

  !> @brief Enable the constant met-source peer and its connector into CECE.
  subroutine set_coupling_metsource(enable)
    logical, intent(in) :: enable
    g_metsource = enable
  end subroutine set_coupling_metsource

  !> @brief True when @p name appears in the comma-separated skip list.
  function in_skip_list(name) result(found)
    character(len=*), intent(in) :: name
    logical :: found
    character(len=512) :: rest, token
    integer :: comma
    found = .false.
    if (len_trim(g_sink_skip) == 0) return
    rest = trim(g_sink_skip) // ','
    do
      comma = index(rest, ',')
      if (comma == 0) exit
      token = trim(rest(1:comma - 1))
      if (len_trim(token) > 0 .and. trim(token) == trim(name)) then
        found = .true.
        return
      end if
      rest = rest(comma + 1:)
    end do
  end function in_skip_list

  !> @brief Convert a null-terminated C character buffer of known length to a
  !> Fortran string. The C-ABI writers always null-terminate and report the
  !> length, so the length is authoritative; the buffer is scanned only to the
  !> reported length.
  function cbuf_to_string(buf, n) result(str)
    character(kind=c_char), intent(in) :: buf(*)
    integer, intent(in) :: n
    character(len=ESMF_MAXSTR) :: str
    integer :: i
    str = ' '
    do i = 1, n
      str(i:i) = buf(i)
    end do
  end function cbuf_to_string

  !> @brief Driver SetServices: derive from NUOPC_Driver and attach the model
  !> and run-sequence specializations.
  subroutine coupling_driver_SS(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    rc = ESMF_SUCCESS

    call NUOPC_CompDerive(driver, driverSS, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_CompSpecialize(driver, specLabel=driver_label_SetModelServices, &
      specRoutine=SetModelServices, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_CompSpecialize(driver, specLabel=driver_label_SetRunSequence, &
      specRoutine=SetRunSequence, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_CompAttributeSet(driver, name="Verbosity", value="high", rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out
  end subroutine coupling_driver_SS

  !> @brief Add CECE, the sink, and the connector between them.
  !!
  !! The sink's import list is populated from CECE's export specs (read from
  !! the config through the C-ABI) before the sink component is added, so its
  !! advertisement mirrors the cap's exactly.
  subroutine SetModelServices(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    type(ESMF_GridComp) :: cece_comp, sink_comp, met_comp
    type(ESMF_CplComp) :: connector, met_connector
    type(ESMF_Time) :: startTime
    type(ESMF_Time) :: stopTime
    type(ESMF_TimeInterval) :: timeStep
    integer :: i, nexp, nsink, c_rc
    integer(c_int) :: count_c, rc_c, nz
    character(kind=c_char), dimension(ESMF_MAXSTR) :: c_species, c_std, c_units, c_name
    integer(c_int) :: species_len, std_len, units_len, name_len
    character(len=ESMF_MAXSTR) :: key, std_name, units, item_name
    character(len=700) :: msg
    integer :: c_path_len
    character(len=64) :: start_time_str, end_time_str
    integer :: timestep_sec

    rc = ESMF_SUCCESS

    ! Point the cap at the config file (same path the sink setup reads).
    call CECE_SetConfigPath(trim(g_config_file), rc)
    if (rc /= ESMF_SUCCESS) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[CouplingDriver] Failed to set CECE config path', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    ! Vertical layer count for the 3-D exports (always from the config).
    call cece_sim_nz_from_config_c(trim(g_config_file)//c_null_char, &
                                   int(len_trim(g_config_file), c_int), nz, c_rc)
    if (c_rc /= 0 .or. int(nz) <= 0) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[CouplingDriver] Could not read vertical layer count from config', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if
    call sink_set_vertical_layers(int(nz))

    ! Sink output directory.
    call sink_set_output_dir(trim(g_sink_dir))

    ! Enumerate CECE's export fields and register a matching sink import for
    ! each, in the same alphabetical order the cap advertises.
    c_path_len = len_trim(g_config_file)
    call cece_nuopc_export_count_c(trim(g_config_file)//c_null_char, &
                                   int(c_path_len, c_int), count_c, rc_c)
    if (rc_c /= 0) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[CouplingDriver] Failed to count CECE export fields', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if
    nexp = int(count_c)

    call sink_begin_fields()
    nsink = 0
    do i = 0, nexp - 1
      call cece_nuopc_export_spec_c(trim(g_config_file)//c_null_char, &
        int(c_path_len, c_int), int(i, c_int), &
        c_species, int(ESMF_MAXSTR, c_int), species_len, &
        c_std, int(ESMF_MAXSTR, c_int), std_len, &
        c_units, int(ESMF_MAXSTR, c_int), units_len, &
        c_name, int(ESMF_MAXSTR, c_int), name_len, rc_c)
      if (rc_c /= 0) then
        write(msg, '(A,I0)') '[CouplingDriver] Failed to read export spec index ', i
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
      key = cbuf_to_string(c_species, int(species_len))
      std_name = cbuf_to_string(c_std, int(std_len))
      units = cbuf_to_string(c_units, int(units_len))
      ! The cap uses the species key as the state item name when no explicit
      ! name is configured; mirror that so the sink's written variable name
      ! matches CECE's standalone output variable.
      if (name_len > 0) then
        item_name = cbuf_to_string(c_name, int(name_len))
      else
        item_name = key
      end if
      ! A skipped export is not advertised by the sink, leaving the matching
      ! CECE export unconnected so the cap prunes it at realization.
      if (in_skip_list(item_name)) then
        write(msg, '(A,A,A)') '[CouplingDriver] Sink skips export "', &
          trim(item_name), '" (left unconnected)'
        call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
        cycle
      end if
      call sink_add_import_field(trim(item_name), trim(std_name), trim(units))
      call sink_end_fields()
      nsink = nsink + 1
    end do

    write(msg, '(A,I0,A,I0,A,I0,A)') '[CouplingDriver] Registered ', nsink, &
      ' of ', nexp, ' sink import field(s), nz=', int(nz)
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)

    ! Build the driver clock from the config timing BEFORE adding components.
    ! Child model components read the driver's internal clock during their own
    ! Initialize phase, which runs before the run sequence is assembled, so a
    ! clock created only in SetRunSequence would be too late.
    c_path_len = len_trim(g_config_file)
    call cece_read_timing_config_c(trim(g_config_file)//c_null_char, &
                                   int(c_path_len, c_int), start_time_str, end_time_str, &
                                   timestep_sec, 64, rc_c)
    c_rc = int(rc_c)
    if (c_rc /= 0) then
      ! The standalone C++ driver requires driver.start_time/end_time and
      ! aborts when missing; the coupling clock must match, so fail loudly.
      write(msg, '(A,A)') '[CouplingDriver] Failed to read timing from ', &
        trim(g_config_file)
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(msg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    call ESMF_TimeSet(startTime, timeString=trim(start_time_str), rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_TimeSet(stopTime, timeString=trim(end_time_str), rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_TimeIntervalSet(timeStep, s=timestep_sec, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    g_clock = ESMF_ClockCreate(timeStep=timeStep, startTime=startTime, &
                               stopTime=stopTime, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_GridCompSet(driver, clock=g_clock, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    write(msg, '(A,A,A,A,A,I0,A)') '[CouplingDriver] Clock: ', &
      trim(start_time_str), ' -> ', trim(end_time_str), ' step ', timestep_sec, 's'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)

    ! Add CECE (component builds its own grid in Realize).
    call NUOPC_DriverAddComp(driver, "CECE", ceceSS, comp=cece_comp, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Add the sink peer.
    call NUOPC_DriverAddComp(driver, "SINK", CECE_SINK_SetServices, &
      comp=sink_comp, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Add the connector from CECE's export to the sink's import. The NUOPC
    ! Layer pairs fields by standard name during initialization. When the sink
    ! advertises nothing every CECE export is unconnected, which is the
    ! single-model case the standalone cap app already exercises, so no
    ! connector is created for it.
    if (nsink > 0) then
      call NUOPC_DriverAddComp(driver, srcCompLabel="CECE", &
        dstCompLabel="SINK", compSetServicesRoutine=connectorSS, &
        comp=connector, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
      call ESMF_LogWrite('[CouplingDriver] Components added: CECE -> SINK', &
        ESMF_LOGMSG_INFO)
    else
      call ESMF_LogWrite('[CouplingDriver] Components added: CECE, SINK'// &
        ' (no connector; all exports unconnected)', ESMF_LOGMSG_INFO)
    end if

    ! Optionally add the constant met source and a connector from its export
    ! into CECE's import. This is the host-forcing path: the met source offers
    ! its own geometry and the Connector copies the values into CECE (default
    ! not-share), the reverse direction from the export coupling above.
    if (g_metsource) then
      call NUOPC_DriverAddComp(driver, "MET", CECE_MET_SOURCE_SetServices, &
        comp=met_comp, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      call NUOPC_DriverAddComp(driver, srcCompLabel="MET", &
        dstCompLabel="CECE", compSetServicesRoutine=connectorSS, &
        comp=met_connector, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
      call ESMF_LogWrite('[CouplingDriver] Components added: MET -> CECE'// &
        ' (import connector)', ESMF_LOGMSG_INFO)
    end if
  end subroutine SetModelServices

  !> @brief Assemble the run sequence: CECE, connector, sink in one slot,
  !> driven by the clock created in SetModelServices.
  subroutine SetRunSequence(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    rc = ESMF_SUCCESS

    ! One time slot. The slot clock must be attached BEFORE any run element is
    ! added: NUOPC_DriverAddRunElement records, for each child model, the
    ! clock currently bound to the slot (runSeq(slot)%clock) as that child's
    ! initialize-time clock. Setting the clock afterwards would leave every
    ! child capturing an invalid clock, and the child's own Initialize would
    ! then fail when it reads its internal clock. This mirrors the default and
    ! free-format driver paths, which both bind the slot clock first.
    call NUOPC_DriverNewRunSequence(driver, slotCount=1, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_DriverSetRunSequence(driver, slot=1, clock=g_clock, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! When the met source is active, its connector into CECE runs first, then
    ! the met source advances, then CECE advances. The met export and CECE
    ! import share storage by reference (both advertise the share policy on an
    ! identical grid distribution), so the connector performs no data regrid:
    ! CECE reads the met source's live storage each step, and ordering the met
    ! source before CECE gives it the current step's value. The connector still
    ! carries the export's timestamp onto the import, and it runs before the met
    ! source advances, so CECE receives the met source's PREVIOUS step-end stamp
    ! (the framework default), which equals CECE's own step-start clock time and
    ! satisfies the consumer's default import-time check.
    if (g_metsource) then
      call NUOPC_DriverAddRunElement(driver, slot=1, srcCompLabel="MET", &
        dstCompLabel="CECE", rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
      call NUOPC_DriverAddRunElement(driver, slot=1, compLabel="MET", rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end if

    call NUOPC_DriverAddRunElement(driver, slot=1, compLabel="CECE", rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    if (sink_has_fields()) then
      call NUOPC_DriverAddRunElement(driver, slot=1, srcCompLabel="CECE", &
        dstCompLabel="SINK", rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end if

    call NUOPC_DriverAddRunElement(driver, slot=1, compLabel="SINK", rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    if (g_metsource) then
      call ESMF_LogWrite('[CouplingDriver] Run sequence set:'// &
        ' (CPL)->MET->CECE->(CPL)->SINK', ESMF_LOGMSG_INFO)
    else
      call ESMF_LogWrite('[CouplingDriver] Run sequence set:'// &
        ' CECE -> (CPL) -> SINK', ESMF_LOGMSG_INFO)
    end if
  end subroutine SetRunSequence

end module coupling_driver_mod

!> @brief Entry point: initialize ESMF, load the field dictionary, run the
!> coupling driver to completion, then tear down.
program test_coupling_app

  use ESMF
  use NUOPC
  use NUOPC_FieldDictionaryAPI, only: NUOPC_FieldDictionarySetup
  use mpi
  use coupling_driver_mod, only: coupling_driver_SS, set_coupling_config, &
    set_coupling_dictionary, set_coupling_sinkdir, set_coupling_sink_skip, &
    set_coupling_metsource

  implicit none

  integer :: rc, userRc, mpierr, nargs, i
  type(ESMF_GridComp) :: drvComp
  character(len=512) :: arg, nextarg
  character(len=512) :: config_file, dict_file, sink_dir, sink_skip
  logical :: have_config, have_dict, metsource

  config_file = ""
  dict_file = ""
  sink_dir = "."
  sink_skip = ""
  metsource = .false.

  nargs = command_argument_count()
  i = 1
  do while (i <= nargs)
    call get_command_argument(i, arg)
    if (trim(arg) == "--config") then
      call get_command_argument(i + 1, nextarg)
      config_file = trim(nextarg)
      i = i + 2
    else if (trim(arg) == "--dictionary") then
      call get_command_argument(i + 1, nextarg)
      dict_file = trim(nextarg)
      i = i + 2
    else if (trim(arg) == "--sinkdir") then
      call get_command_argument(i + 1, nextarg)
      sink_dir = trim(nextarg)
      i = i + 2
    else if (trim(arg) == "--sink-skip") then
      call get_command_argument(i + 1, nextarg)
      sink_skip = trim(nextarg)
      i = i + 2
    else if (trim(arg) == "--metsource") then
      metsource = .true.
      i = i + 1
    else
      i = i + 1
    end if
  end do

  have_config = (len_trim(config_file) > 0)
  have_dict = (len_trim(dict_file) > 0)
  if (.not. have_config) then
    write(*,'(A)') "ERROR: --config <yaml> is required"
    call exit(2)
  end if
  if (.not. have_dict) then
    write(*,'(A)') "ERROR: --dictionary <yaml> is required"
    call exit(2)
  end if

  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_GREGORIAN, &
                       logkindflag=ESMF_LOGKIND_MULTI, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "ERROR: ESMF_Initialize failed rc=", rc
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if

  ! Load the field dictionary as the host application, before any component
  ! advertises. The preloaded ESMF dictionary lacks the emission standard names
  ! CECE advertises, so without this the Advertise phase rejects them.
  call NUOPC_FieldDictionarySetup(trim(dict_file), rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  call set_coupling_config(trim(config_file))
  call set_coupling_dictionary(trim(dict_file))
  call set_coupling_sinkdir(trim(sink_dir))
  call set_coupling_sink_skip(trim(sink_skip))
  call set_coupling_metsource(metsource)

  write(*,'(A,A)') "INFO: [couplingApp] config:     ", trim(config_file)
  write(*,'(A,A)') "INFO: [couplingApp] dictionary: ", trim(dict_file)
  write(*,'(A,A)') "INFO: [couplingApp] sinkdir:    ", trim(sink_dir)
  write(*,'(A,L1)') "INFO: [couplingApp] metsource:  ", metsource

  drvComp = ESMF_GridCompCreate(name="coupling_driver", rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  call ESMF_GridCompSetServices(drvComp, coupling_driver_SS, userRc=userRc, rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  if (ESMF_LogFoundError(rcToCheck=userRc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  call ESMF_GridCompInitialize(drvComp, userRc=userRc, rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  if (ESMF_LogFoundError(rcToCheck=userRc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  call ESMF_GridCompRun(drvComp, userRc=userRc, rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  if (ESMF_LogFoundError(rcToCheck=userRc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  call ESMF_GridCompFinalize(drvComp, userRc=userRc, rc=rc)
  if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  if (ESMF_LogFoundError(rcToCheck=userRc, msg=ESMF_LOGERR_PASSTHRU, &
    line=__LINE__, file=__FILE__)) &
    call ESMF_Finalize(endflag=ESMF_END_ABORT)

  call ESMF_LogWrite("test_coupling_app FINISHED", ESMF_LOGMSG_INFO, rc=rc)
  write(*,'(A)') "INFO: [couplingApp] coupling run completed successfully"

  call ESMF_Finalize(endflag=ESMF_END_KEEPMPI, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(*,'(A,I0)') "WARN: [couplingApp] ESMF_Finalize reported rc=", rc
  end if

  call MPI_Finalize(mpierr)
  if (mpierr /= MPI_SUCCESS) then
    write(*,'(A,I0)') "WARN: [couplingApp] MPI_Finalize returned error code ", mpierr
  end if

end program test_coupling_app
