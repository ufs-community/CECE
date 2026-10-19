!> @file test_diagnostics_coupled.F90
!> @brief Real-ESMF integration test for diagnostics through the NUOPC cap
!> (feature 003, US3 / T023). Constitution II: no mocking — all ESMF/NUOPC.
!>
!> Hosts the PRODUCTION CECE cap under a minimal NUOPC_Driver (the same
!> single-model pattern as src/driver/nuopc/standalone), runs Initialize +
!> one Advance, then inspects the cap's export state through the test
!> accessors CECE_GetExportState / CECE_GetSimPtr.
!>
!> Because cece_core_finalize tears down Kokkos when CECE initialized it, and
!> Kokkos cannot be re-initialized after finalization within one process
!> (Kokkos core wiki, is_initialized_or_finalized Notes), each scenario runs
!> as its own process, selected by argv(1):
!>
!>   values  (quickstart US3 #1 + #2, FR-016 + FR-015/SC-006):
!>     - exportState carries the selected diagnostic by name;
!>     - its realized Units/LongName equal the registry (metadata is stamped
!>       on the field before NUOPC_Realize — Advertise args are not observable
!>       on the state item in ESMF 8.9.1);
!>     - the ESMF field buffer equals the registry host view for the same step
!>       (single rank: band == full grid, element-wise compare), and equals the
!>       analytic anomaly (0 - 298.15), so an all-zeros buffer cannot pass;
!>     - the realization-abort guard (US3 #4) fires with the name in the
!>       message: cece_sim_diagnostic_units / cece_sim_bind_diagnostic_field
!>       return rc<0 for a name no scheme registered.
!>
!>   optout  (quickstart US3 #3, FR-010):
!>     - include_diagnostics removed => the path query advertises zero names
!>       and exportState holds no diagnostic channel.
!>
!> The harness runs the two modes as separate CTest cases (see
!> tests/CMakeLists.txt) and passes the rendered config path as argv(2).
module diag_test_driver_mod

  use ESMF
  use NUOPC
  use NUOPC_Driver, driverSS => SetServices
  use NUOPC_Driver, only: driver_label_SetModelServices => label_SetModelServices
  use NUOPC_Driver, only: NUOPC_DriverAddComp
  use cece_cap_mod, ceceSS => CECE_SetServices
  use cece_cap_mod, only: CECE_SetConfigPath
  implicit none

  private
  public :: diag_test_driver_SS, diag_test_set_config

  character(len=512), save :: g_config = "cece_config_diagnostics.yaml"

contains

  subroutine diag_test_set_config(config_file)
    character(len=*), intent(in) :: config_file
    g_config = config_file
  end subroutine diag_test_set_config

  !> @brief Driver services: derive from the generic NUOPC driver and attach
  !> ONLY the CECE model (no connector, no peer — single-component host).
  subroutine diag_test_driver_SS(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    rc = ESMF_SUCCESS
    call NUOPC_CompDerive(driver, driverSS, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    call NUOPC_CompSpecialize(driver, specLabel=driver_label_SetModelServices, &
                              specRoutine=SetModelServices, rc=rc)
  end subroutine diag_test_driver_SS

  subroutine SetModelServices(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    type(ESMF_GridComp) :: child
    integer :: c_rc

    rc = ESMF_SUCCESS
    call CECE_SetConfigPath(trim(g_config), rc)
    if (rc /= ESMF_SUCCESS) return
    call NUOPC_DriverAddComp(driver, "CECE", ceceSS, comp=child, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! The child reads the driver's clock during its own Initialize, so set it
    ! here (before Initialize), mirroring the production standalone driver.
    call set_driver_clock(driver, rc)
  end subroutine SetModelServices

  subroutine set_driver_clock(driver, rc)
    type(ESMF_GridComp) :: driver
    integer, intent(out) :: rc

    type(ESMF_Clock) :: clock
    type(ESMF_Time) :: startTime, stopTime
    type(ESMF_TimeInterval) :: timeStep

    rc = ESMF_SUCCESS
    call ESMF_TimeSet(startTime, yy=2020, mm=6, dd=1, h=0, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    call ESMF_TimeSet(stopTime, yy=2020, mm=6, dd=1, h=1, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    call ESMF_TimeIntervalSet(timeStep, s=3600, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    clock = ESMF_ClockCreate(timeStep=timeStep, startTime=startTime, &
                             stopTime=stopTime, rc=rc)
    if (rc /= ESMF_SUCCESS) return
    call ESMF_GridCompSet(driver, clock=clock, rc=rc)
  end subroutine set_driver_clock

end module diag_test_driver_mod

program test_diagnostics_coupled

  use ESMF
  use NUOPC
  use diag_test_driver_mod, only: diag_test_driver_SS, diag_test_set_config
  use cece_cap_mod, only: CECE_GetSimPtr, CECE_GetExportState
  use, intrinsic :: iso_c_binding
  use mpi

  implicit none

  interface
    ! Path-based advertisement count (gate + selection from the config).
    subroutine cece_nuopc_diagnostic_count(config_path, path_len, count, rc) &
                                       bind(C, name="cece_nuopc_diagnostic_count")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine

    ! Registry metadata reader — the realization-abort guard path.
    subroutine cece_sim_diagnostic_units(sim, name, name_len, units, units_cap, &
                                         units_len, rc) &
                                         bind(C, name="cece_sim_diagnostic_units")
      import :: c_char, c_int, c_ptr
      type(c_ptr), value :: sim
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len
      character(kind=c_char), intent(out) :: units(*)
      integer(c_int), value :: units_cap
      integer(c_int), intent(out) :: units_len
      integer(c_int), intent(out) :: rc
    end subroutine

    ! Diagnostic binder — the other realization-abort guard path.
    subroutine cece_sim_bind_diagnostic_field(sim, name, name_len, data_ptr, &
                                              nx, ny_local, nz, rc) &
                                              bind(C, name="cece_sim_bind_diagnostic_field")
      import :: c_char, c_int, c_ptr
      type(c_ptr), value :: sim
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny_local, nz
      integer(c_int), intent(out) :: rc
    end subroutine

    ! Registry full-grid host copy — the value-equality oracle.
    subroutine cece_sim_diagnostic_host_copy(sim, name, name_len, out, out_max, &
                                             count_out, rc) &
                                             bind(C, name="cece_sim_diagnostic_host_copy")
      import :: c_char, c_int, c_ptr, c_double
      type(c_ptr), value :: sim
      character(kind=c_char), intent(in) :: name(*)
      integer(c_int), value :: name_len
      real(c_double), intent(out) :: out(*)
      integer(c_int), value :: out_max
      integer(c_int), intent(out) :: count_out
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

  integer :: rc, userRc, pet, psize, mpierr
  integer :: nargs
  character(len=64) :: mode
  character(len=512) :: config_file
  type(ESMF_GridComp) :: drv

  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_GREGORIAN, &
                       logkindflag=ESMF_LOGKIND_NONE, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    write(0,'(A,I0)') 'FATAL: ESMF_Initialize failed rc=', rc
    call exit(1)
  end if

  call MPI_Comm_rank(MPI_COMM_WORLD, pet, mpierr)
  call MPI_Comm_size(MPI_COMM_WORLD, psize, mpierr)
  if (psize /= 1) then
    if (pet == 0) write(0,'(A,I0)') 'FATAL: single-rank test, np=', psize
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end if

  nargs = command_argument_count()
  mode = 'values'
  if (nargs >= 1) call get_command_argument(1, mode)
  config_file = 'cece_config_diagnostics.yaml'
  if (nargs >= 2) call get_command_argument(2, config_file)

  call diag_test_set_config(trim(config_file))

  drv = ESMF_GridCompCreate(name='diag_test_driver', rc=rc)
  if (rc /= ESMF_SUCCESS) call die('GridCompCreate', rc)
  call ESMF_GridCompSetServices(drv, diag_test_driver_SS, userRc=userRc, rc=rc)
  if (rc /= ESMF_SUCCESS) call die('SetServices rc', rc)
  if (userRc /= ESMF_SUCCESS) call die('SetServices userRc', userRc)

  call ESMF_GridCompInitialize(drv, userRc=userRc, rc=rc)
  if (rc /= ESMF_SUCCESS) call die('Initialize rc', rc)
  if (userRc /= ESMF_SUCCESS) call die('Initialize userRc', userRc)

  call ESMF_GridCompRun(drv, userRc=userRc, rc=rc)
  if (rc /= ESMF_SUCCESS) call die('Run rc', rc)
  if (userRc /= ESMF_SUCCESS) call die('Run userRc', userRc)

  ! Inspect while the simulation is still live (before Finalize tears down
  ! Kokkos and the registry).
  select case (trim(mode))
  case ('optout')
    call check_optout()
  case default
    call check_values()
  end select

  call ESMF_GridCompFinalize(drv, userRc=userRc, rc=rc)
  if (rc /= ESMF_SUCCESS) call die('Finalize rc', rc)
  if (userRc /= ESMF_SUCCESS) call die('Finalize userRc', userRc)

  call ESMF_Finalize(endflag=ESMF_END_KEEPMPI, rc=rc)
  call MPI_Finalize(mpierr)
  if (pet == 0) write(*,'(A,A,A)') 'PASS: ', trim(mode), ' diagnostics coupled checks'

contains

  !> @brief US3 #1 + #2 + #4 (guard): metadata, values, and the abort guard.
  subroutine check_values()
    type(ESMF_State) :: exportState
    type(ESMF_Field) :: field
    type(c_ptr) :: sim
    character(len=ESMF_MAXSTR) :: units, long_name
    real(c_double), pointer :: fptr(:, :, :)
    real(c_double), allocatable :: reg(:)
    integer :: al(3), au(3), fnx, fny, fnz, n_field, i
    integer(c_int) :: c_rc, count_c
    character(len=ESMF_MAXSTR) :: probe_units
    character(len=200) :: emsg
    integer(c_int) :: probe_len
    type(c_ptr) :: dummy_ptr
    real(c_double), target :: dummy_val

    exportState = CECE_GetExportState()

    ! #1: the selected diagnostic is present by its advertised name.
    call ESMF_StateGet(exportState, 'temperature_anomaly', field, rc=rc)
    if (rc /= ESMF_SUCCESS) call die('temperature_anomaly absent from exportState', rc)

    ! FR-016: realized Units/LongName equal the registry.
    call ESMF_AttributeGet(field, name='Units', value=units, rc=rc)
    if (rc /= ESMF_SUCCESS) call die('Units attribute missing', rc)
    if (trim(units) /= 'K') call fail('Units != K')
    call ESMF_AttributeGet(field, name='LongName', value=long_name, rc=rc)
    if (rc /= ESMF_SUCCESS) call die('LongName attribute missing', rc)
    if (trim(long_name) /= 'Temperature deviation from reference') call fail('LongName mismatch')

    ! #2: ESMF buffer == registry host view, element-wise (single rank).
    nullify(fptr)
    call ESMF_FieldGet(field, farrayPtr=fptr, rc=rc)
    if (rc /= ESMF_SUCCESS) call die('no farrayPtr', rc)
    al = lbound(fptr)
    au = ubound(fptr)
    fnx = au(1) - al(1) + 1
    fny = au(2) - al(2) + 1
    fnz = au(3) - al(3) + 1
    n_field = fnx * fny * fnz
    if (n_field <= 0) call fail('empty field storage')

    sim = CECE_GetSimPtr()
    if (.not. c_associated(sim)) call fail('sim handle null after run')

    allocate(reg(n_field))
    call cece_sim_diagnostic_host_copy(sim, 'temperature_anomaly'//c_null_char, &
      int(len('temperature_anomaly'), c_int), reg, int(n_field, c_int), count_c, c_rc)
    if (c_rc /= 0) call die('registry host copy failed', int(c_rc))
    if (int(count_c) /= n_field) call fail('registry size != field size')

    do i = 1, n_field
      if (fptr(al(1) + mod(i - 1, fnx), &
               al(2) + mod((i - 1) / fnx, fny), &
               al(3) + (i - 1) / (fnx * fny)) /= reg(i)) then
        write(emsg, '(A,I0,A,ES18.10,A,ES18.10)') 'ESMF value differs from registry at element ', &
          i, ' fptr=', fptr(al(1) + mod(i - 1, fnx), al(2) + mod((i - 1) / fnx, fny), &
          al(3) + (i - 1) / (fnx * fny)), ' reg=', reg(i)
        call fail(trim(emsg))
      end if
    end do

    ! Pin the analytic value (no temperature input => 0 - 298.15 everywhere),
    ! so a vacuous all-equal-but-zero comparison cannot pass.
    if (reg(1) /= -298.15_c_double) call fail('registry value is not -298.15')

    ! #4 (guard): a name no scheme registered must fail the metadata read and
    ! the binder, each with rc<0. (The config path structurally prevents an
    ! advertised-but-unregistered name; this proves the guard that protects
    ! realization if the two ever diverge.)
    probe_units = ' '
    probe_len = 0
    call cece_sim_diagnostic_units(sim, 'never_registered_diag'//c_null_char, &
      int(len('never_registered_diag'), c_int), probe_units, &
      int(ESMF_MAXSTR, c_int), probe_len, c_rc)
    if (c_rc == 0) call fail('metadata read succeeded for an unregistered name')

    dummy_val = 0.0_c_double
    dummy_ptr = C_LOC(dummy_val)
    call cece_sim_bind_diagnostic_field(sim, 'never_registered_diag'//c_null_char, &
      int(len('never_registered_diag'), c_int), dummy_ptr, &
      int(fnx, c_int), int(fny, c_int), int(fnz, c_int), c_rc)
    if (c_rc == 0) call fail('bind succeeded for an unregistered name')

    deallocate(reg)
  end subroutine check_values

  !> @brief US3 #3 (FR-010): include_diagnostics removed => no channel.
  subroutine check_optout()
    type(ESMF_State) :: exportState
    type(ESMF_Field) :: field
    integer(c_int) :: count_c, c_rc
    integer :: nitems

    ! The path query must report zero advertised names for this config.
    call cece_nuopc_diagnostic_count(trim(config_file)//c_null_char, &
      int(len_trim(config_file), c_int), count_c, c_rc)
    if (c_rc /= 0) call die('diagnostic count query failed', int(c_rc))
    if (int(count_c) /= 0) call fail('path query advertised names with include_diagnostics off')

    ! And the export state must contain no diagnostic channel.
    exportState = CECE_GetExportState()
    call ESMF_StateGet(exportState, 'temperature_anomaly', field, rc=rc)
    if (rc == ESMF_SUCCESS) call fail('diagnostic present with include_diagnostics off')
    rc = ESMF_SUCCESS

    nitems = -1
    call ESMF_StateGet(exportState, itemCount=nitems, rc=rc)
    if (rc /= ESMF_SUCCESS) call die('itemCount read failed', rc)
    ! This fixture has no species exports either; the state must be empty.
    if (nitems /= 0) call fail('exportState not empty with diagnostics off')
  end subroutine check_optout

  subroutine die(msg, code)
    character(len=*), intent(in) :: msg
    integer, intent(in) :: code
    write(0,'(A,A,A,I0)') 'FATAL: ', trim(msg), ' rc=', code
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end subroutine die

  subroutine fail(msg)
    character(len=*), intent(in) :: msg
    write(0,'(A,A)') 'FATAL: ', trim(msg)
    call ESMF_Finalize(endflag=ESMF_END_ABORT)
  end subroutine fail

end program test_diagnostics_coupled
