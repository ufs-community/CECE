!> @file cece_cap.F90
!> @brief Production-grade decoupled NUOPC Model cap for CECE.
!>
!> The cap is a thin adapter over the shared simulation contract
!> (cece_sim_* C ABI): all lifecycle sequencing, export-field registration,
!> the time convention (ingest at step start, file output stamped at step
!> end, coupled export timestamps stamped at step start), and
!> teardown live inside the shared core so they cannot diverge from the
!> C++ standalone driver. The two drivers differ ONLY in how they obtain
!> the target grid; this cap resolves it from the CECE YAML through the
!> same code path the C++ driver runs.
module cece_cap_mod
  use iso_c_binding
  use ESMF
  use NUOPC
  use NUOPC_Model, modelSS => SetServices
  use NUOPC_Model, only: &
    label_Advertise, &
    label_RealizeProvided, &
    label_RealizeAccepted, &
    label_TimestampExport, &
    model_label_Advance => label_Advance, &
    model_label_Finalize => label_Finalize
  use cece_cap_grid_mod
  implicit none

  private

  public :: CECE_SetServices, CECE_SetConfigPath

  !> @brief Shared simulation handle (opaque cece::CeceSimulation*).
  type(c_ptr), save :: g_sim_ptr = c_null_ptr

  !> @brief Module-level config file path (save ensures persistence across phases).
  character(len=512), save :: g_config_file_path = "cece_control_mock.yaml"

  !> @brief Monotonic step counter; the shared writer counts steps 1-based.
  integer, save :: g_step_count = 0

  !> @brief Latched once the shared core reports completion; further
  !> advances then do no work (contract: hosts must stop stepping on the
  !> same signal as the C++ driver).
  logical, save :: g_complete = .false.

  !> @brief Step-start instant captured during Advance. Export fields are
  !> stamped with this time by the TimestampExport specialization: a
  !> consumer's default CheckImport compares import timestamps against its
  !> clock currTime, which during a driver sweep equals the step start.
  !> The framework default would stamp at step end, which is a full step
  !> ahead of that check and fails whenever fields are reference-shared
  !> (shared fields are seen live, with no connector lag to hide it).
  type(ESMF_Time), save :: g_step_start_time

  !> @brief State-item names of the NUOPC import fields that were connected
  !> and realized on the component grid. Cached at realization so each Run
  !> copies exactly those fields' host storage into the simulation, without
  !> re-walking the config list (which also names unconnected imports that
  !> were pruned and must not be touched). Absent when no import is connected.
  character(len=ESMF_MAXSTR), allocatable, save :: g_import_names(:)

  ! C interfaces to the shared simulation facade (cece_driver library).
  ! These mirror include/cece/cece_sim_c_abi.h exactly.
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

    subroutine cece_sim_create_from_yaml(config_path, path_len, mpi_comm_f, out_sim, rc) &
                                         bind(C, name="cece_sim_create_from_yaml")
      import :: c_char, c_int, c_ptr
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), value :: mpi_comm_f
      type(c_ptr), intent(out) :: out_sim
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_sim_step(sim, step_start_iso, step_start_len, &
                             step_end_iso, step_end_len, &
                             step_index, complete_out, rc) &
                             bind(C, name="cece_sim_step")
      import :: c_char, c_int, c_ptr
      type(c_ptr), value :: sim
      character(kind=c_char), intent(in) :: step_start_iso(*), step_end_iso(*)
      integer(c_int), value :: step_start_len, step_end_len
      integer(c_int), value :: step_index
      integer(c_int), intent(out) :: complete_out
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_sim_finalize(sim, rc) &
                             bind(C, name="cece_sim_finalize")
      import :: c_ptr, c_int
      type(c_ptr), value :: sim
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_sim_grid_info(sim, nx, ny, nz, topology, &
                                  lon_min, lon_max, lat_min, lat_max, rc) &
                                  bind(C, name="cece_sim_grid_info")
      import :: c_ptr, c_int, c_double
      type(c_ptr), value :: sim
      integer(c_int), intent(out) :: nx, ny, nz, topology
      real(c_double), intent(out) :: lon_min, lon_max, lat_min, lat_max
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_sim_nz_from_config(config_path, path_len, nz_out, rc) &
                                       bind(C, name="cece_sim_nz_from_config")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), intent(out) :: nz_out
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_sim_describe_yaml_grid(config_path, path_len, buf, &
                                           buf_max, buf_len_out, rc) &
                                           bind(C, name="cece_sim_describe_yaml_grid")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      character(kind=c_char), intent(out) :: buf(*)
      integer(c_int), value :: buf_max
      integer(c_int), intent(out) :: buf_len_out
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_sim_create_from_esmf(config_path, path_len, nx, ny, nz, &
                                         is_rad, lon_coords, lon_len, &
                                         lat_coords, lat_len, mpi_comm_f, &
                                         out_sim, rc) &
                                         bind(C, name="cece_sim_create_from_esmf")
      import :: c_char, c_int, c_ptr, c_double
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), value :: nx, ny, nz, is_rad
      real(c_double), intent(in) :: lon_coords(*)
      integer(c_int), value :: lon_len
      real(c_double), intent(in) :: lat_coords(*)
      integer(c_int), value :: lat_len
      integer(c_int), value :: mpi_comm_f
      type(c_ptr), intent(out) :: out_sim
      integer(c_int), intent(out) :: rc
    end subroutine

    ! Rebinds the core's persistent write-back target for `species` to the
    ! ESMF field's own storage. Pointer-map update only; ESMF owns the memory.
    subroutine cece_sim_bind_export_field(sim, species, species_len, data_ptr, &
                                          nx, ny_local, nz, rc) &
                                          bind(C, name="cece_sim_bind_export_field")
      import :: c_char, c_int, c_ptr, c_double
      type(c_ptr), value :: sim
      character(kind=c_char), intent(in) :: species(*)
      integer(c_int), value :: species_len
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny_local, nz
      integer(c_int), intent(out) :: rc
    end subroutine

    ! Copies a connected import field's ESMF-owned host storage into the
    ! core's import state for the current step. The configured input name is
    ! resolved through the met/scale/mask mappings inside the facade; the cap
    ! only passes the field's own per-PET extents.
    subroutine cece_sim_set_import_field(sim, field, field_len, data_ptr, &
                                         nx, ny_local, rc) &
                                         bind(C, name="cece_sim_set_import_field")
      import :: c_char, c_int, c_ptr, c_double
      type(c_ptr), value :: sim
      character(kind=c_char), intent(in) :: field(*)
      integer(c_int), value :: field_len
      type(c_ptr), value :: data_ptr
      integer(c_int), value :: nx, ny_local
      integer(c_int), intent(out) :: rc
    end subroutine

    ! Path-based NUOPC coupling config queries (cece_core library). They read
    ! the optional `nuopc:` section straight from the YAML, so the cap can
    ! advertise its fields before the simulation exists. Lists come back sorted
    ! alphabetically by key, making the advertisement order identical on every
    ! rank. Each writer null-terminates and reports the string length; an empty
    ! optional attribute returns length zero.
    subroutine cece_nuopc_export_count(config_path, path_len, count, rc) &
                                        bind(C, name="cece_nuopc_export_count")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_nuopc_export_spec(config_path, path_len, index, &
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

    subroutine cece_nuopc_import_count(config_path, path_len, count, rc) &
                                        bind(C, name="cece_nuopc_import_count")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len
      integer(c_int), intent(out) :: count
      integer(c_int), intent(out) :: rc
    end subroutine

    subroutine cece_nuopc_import_spec(config_path, path_len, index, &
                                      field, field_cap, field_len, &
                                      std_name, std_cap, std_len, &
                                      units, units_cap, units_len, &
                                      name, name_cap, name_len, rc) &
                                      bind(C, name="cece_nuopc_import_spec")
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value :: path_len, index
      character(kind=c_char), intent(out) :: field(*), std_name(*), units(*), name(*)
      integer(c_int), value :: field_cap, std_cap, units_cap, name_cap
      integer(c_int), intent(out) :: field_len, std_len, units_len, name_len
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

    rc = ESMF_SUCCESS

    call ESMF_LogWrite('[Cap] CECE_SetServices entered', ESMF_LOGMSG_INFO)

    ! 1. Inherit NUOPC Model base services
    call NUOPC_CompDerive(gcomp, modelSS, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! 2. Register initialization phase 1 (Advertise)
    call NUOPC_CompSpecialize(gcomp, specLabel=label_Advertise, &
      specRoutine=InitializeAdvertise, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! 3. Register initialization phase 2 (Realize)
    call NUOPC_CompSpecialize(gcomp, specLabel=label_RealizeProvided, &
      specRoutine=InitializeRealize, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! 3b. Register the accepted-import realization. CECE's imports accept the
    ! host's geometry ("cannot provide"), so their fields are allocated on the
    ! transferred grid in this phase, after the framework has moved the
    ! provider's geometry across. This mirrors the export side's provider
    ! realization and is where unconnected imports get pruned.
    call NUOPC_CompSpecialize(gcomp, specLabel=label_RealizeAccepted, &
      specRoutine=RealizeAccepted, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! 4. Register Run (Advance) phase
    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Advance, &
      specRoutine=Run, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! 5. Override the export timestamping specialization point. The Model
    ! wrapper attaches a default that stamps exports at the clock's current
    ! time after the step loop (step end); coupled consumers check imports at
    ! step start instead, so shared exports must carry the step-start stamp.
    call NUOPC_CompSpecialize(gcomp, specLabel=label_TimestampExport, &
      specRoutine=StampExports, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! 6. Register Finalize phase
    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Finalize, &
      specRoutine=Finalize, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_LogWrite('[Cap] CECE_SetServices completed successfully', &
      ESMF_LOGMSG_INFO)
  end subroutine CECE_SetServices

  !> @brief InitializeAdvertise (IPDv01p1)
  !>
  !> Records the configuration path, sets up run logging/banner, and
  !> advertises every emission export configured in the `nuopc:` section of
  !> the CECE YAML. The lists come from the path-based config queries, which
  !> return them sorted alphabetically, so the advertisement order is
  !> deterministic and identical on every rank. The simulation itself is built
  !> in InitializeRealize, where the target grid is resolved — a parent
  !> component can only provide its grid once realization has begun, so
  !> construction must wait for that phase.
  subroutine InitializeAdvertise(comp, rc)
    type(ESMF_GridComp)  :: comp
    integer, intent(out) :: rc

    type(ESMF_State) :: exportState
    type(ESMF_State) :: importState
    integer(c_int) :: count_c, c_rc
    integer :: i, nexp, nimp
    character(kind=c_char), dimension(ESMF_MAXSTR) :: c_species, c_std, c_units, c_name
    integer(c_int) :: species_len, std_len, units_len, name_len
    character(len=ESMF_MAXSTR) :: key, std_name, units, item_name
    character(len=700) :: msg

    rc = ESMF_SUCCESS
    call ESMF_LogWrite('[Cap] InitializeAdvertise entered', ESMF_LOGMSG_INFO)

    ! Set YAML configuration path in the core C-API
    call cece_set_config_file_path(trim(g_config_file_path)//c_null_char, &
                                   int(len_trim(g_config_file_path), c_int))

    ! Configure run logging (optional log file, per-rank stdout suppression) and
    ! print the startup banner. Shared with the standalone driver so behavior is
    ! identical regardless of how CECE is launched. The shared simulation core
    ! re-invokes both during creation; the redirect, banner, and path are
    ! idempotent, but the banner must appear before grid resolution, so the cap
    ! calls them here too.
    call cece_run_log_setup(trim(g_config_file_path)//c_null_char, &
                            int(len_trim(g_config_file_path), c_int))

    ! Advertise the configured emission exports. CECE is the provider for its
    ! own export fields, so it offers the geometry ("will provide"); a peer
    ! that accepts the transfer receives CECE's grid. With no `nuopc:` section
    ! the count is zero and nothing is advertised, which keeps a standalone
    ! cap run byte-for-byte identical to before coupling support.
    call NUOPC_ModelGet(comp, exportState=exportState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call cece_nuopc_export_count(trim(g_config_file_path)//c_null_char, &
                                 int(len_trim(g_config_file_path), c_int), &
                                 count_c, c_rc)
    if (c_rc /= 0) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[Cap] Failed to count configured NUOPC export fields', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if
    nexp = int(count_c)

    do i = 0, nexp - 1
      call cece_nuopc_export_spec(trim(g_config_file_path)//c_null_char, &
        int(len_trim(g_config_file_path), c_int), int(i, c_int), &
        c_species, int(ESMF_MAXSTR, c_int), species_len, &
        c_std, int(ESMF_MAXSTR, c_int), std_len, &
        c_units, int(ESMF_MAXSTR, c_int), units_len, &
        c_name, int(ESMF_MAXSTR, c_int), name_len, c_rc)
      if (c_rc /= 0) then
        write(msg, '(A,I0)') '[Cap] Failed to read NUOPC export spec at index ', i
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
      key = nuopc_cbuf_to_string(c_species, int(species_len))
      std_name = nuopc_cbuf_to_string(c_std, int(std_len))
      units = nuopc_cbuf_to_string(c_units, int(units_len))
      ! The state item name defaults to the species key unless the config
      ! supplies an explicit name.
      if (name_len > 0) then
        item_name = nuopc_cbuf_to_string(c_name, int(name_len))
      else
        item_name = key
      end if

      ! CECE is the geometry provider for its own exports. Both share
      ! policies must be requested explicitly: NUOPC_Advertise defaults them
      ! to "not share", which would send the Connector down the remap path
      ! instead of handing the connected consumer a reference to CECE's
      ! storage. Sharing also requires identical PET distribution on both
      ! sides, which holds here because the acceptor realizes on CECE's
      ! transferred grid.
      if (units_len > 0) then
        call NUOPC_Advertise(exportState, StandardName=trim(std_name), &
          name=trim(item_name), Units=trim(units), &
          TransferOfferGeomObject='will provide', &
          SharePolicyField='share', SharePolicyGeomObject='share', rc=rc)
      else
        ! No units configured: let the field dictionary's canonical units apply.
        call NUOPC_Advertise(exportState, StandardName=trim(std_name), &
          name=trim(item_name), &
          TransferOfferGeomObject='will provide', &
          SharePolicyField='share', SharePolicyGeomObject='share', rc=rc)
      end if
      if (rc /= ESMF_SUCCESS) then
        write(msg, '(A,A,A)') '[Cap] Failed to advertise export field "', &
          trim(item_name), '" (standard name not in the field dictionary?)'
        call ESMF_LogSetError(rcToCheck=rc, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
    end do

    write(msg, '(A,I0,A)') '[Cap] Advertised ', nexp, &
      ' NUOPC export field(s) from the config'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)

    ! Advertise the configured host-provided imports. CECE is the acceptor for
    ! these fields, so it declares it cannot provide their geometry
    ! ("cannot provide") and the Connector transfers the host's grid across.
    ! The default (not share) policies are requested implicitly by omitting
    ! them: a copied import is what the host-forcing path supplies, and the
    ! cap reads the values each step rather than aliasing host storage. With no
    ! import list the count is zero and nothing is advertised, so a standalone
    ! cap run is unchanged.
    call NUOPC_ModelGet(comp, importState=importState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call cece_nuopc_import_count(trim(g_config_file_path)//c_null_char, &
                                 int(len_trim(g_config_file_path), c_int), &
                                 count_c, c_rc)
    if (c_rc /= 0) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[Cap] Failed to count configured NUOPC import fields', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if
    nimp = int(count_c)

    do i = 0, nimp - 1
      call cece_nuopc_import_spec(trim(g_config_file_path)//c_null_char, &
        int(len_trim(g_config_file_path), c_int), int(i, c_int), &
        c_species, int(ESMF_MAXSTR, c_int), species_len, &
        c_std, int(ESMF_MAXSTR, c_int), std_len, &
        c_units, int(ESMF_MAXSTR, c_int), units_len, &
        c_name, int(ESMF_MAXSTR, c_int), name_len, c_rc)
      if (c_rc /= 0) then
        write(msg, '(A,I0)') '[Cap] Failed to read NUOPC import spec at index ', i
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
      key = nuopc_cbuf_to_string(c_species, int(species_len))
      std_name = nuopc_cbuf_to_string(c_std, int(std_len))
      units = nuopc_cbuf_to_string(c_units, int(units_len))
      ! The state item name defaults to the configured input key unless the
      ! config supplies an explicit name (same rule as the exports).
      if (name_len > 0) then
        item_name = nuopc_cbuf_to_string(c_name, int(name_len))
      else
        item_name = key
      end if

      ! Reference sharing must be requested on BOTH sides: NUOPC_Advertise
      ! defaults the share policies to "not share", which would make the
      ! Connector regrid the met grid instead of aliasing storage. CECE
      ! realizes this import on the transferred met grid, so the two sides
      ! share an identical data distribution and the connector can alias.
      if (units_len > 0) then
        call NUOPC_Advertise(importState, StandardName=trim(std_name), &
          name=trim(item_name), Units=trim(units), &
          TransferOfferGeomObject='cannot provide', &
          SharePolicyField='share', SharePolicyGeomObject='share', rc=rc)
      else
        call NUOPC_Advertise(importState, StandardName=trim(std_name), &
          name=trim(item_name), &
          TransferOfferGeomObject='cannot provide', &
          SharePolicyField='share', SharePolicyGeomObject='share', rc=rc)
      end if
      if (rc /= ESMF_SUCCESS) then
        write(msg, '(A,A,A)') '[Cap] Failed to advertise import field "', &
          trim(item_name), '" (standard name not in the field dictionary?)'
        call ESMF_LogSetError(rcToCheck=rc, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
    end do

    write(msg, '(A,I0,A)') '[Cap] Advertised ', nimp, &
      ' NUOPC import field(s) from the config'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)

    call ESMF_LogWrite('[Cap] InitializeAdvertise completed successfully', &
      ESMF_LOGMSG_INFO)
  end subroutine InitializeAdvertise

  !> @brief Convert a null-terminated C character buffer of reported length
  !> into a Fortran string. The config-query C-ABI writers always
  !> null-terminate and return the length, so the length is authoritative and
  !> the buffer is scanned only that far.
  function nuopc_cbuf_to_string(buf, n) result(str)
    character(kind=c_char), intent(in) :: buf(*)
    integer, intent(in) :: n
    character(len=ESMF_MAXSTR) :: str
    integer :: i
    str = ' '
    do i = 1, n
      str(i:i) = buf(i)
    end do
  end function nuopc_cbuf_to_string

  !> @brief InitializeRealize (IPDv01p3)
  !>
  !> Resolves the target grid and builds the simulation through the shared
  !> facade. Grid resolution order: a parent-provided ESMF Grid/Mesh (a
  !> coupled run associates one with the component before realization) wins;
  !> otherwise the grid comes from the CECE YAML via the exact same code path
  !> as the C++ driver. When both exist, the parent grid is used and a single
  !> warning names the ignored YAML grid — the two sources are never merged.
  !> An unsupported parent topology fails loudly at realization; there is no
  !> fallback to a uniform grid. The vertical layer count always comes from
  !> the config, never from the flat 2-D grid.
  !>
  !> Standalone case (no parent grid): after the simulation is built, the
  !> component is associated with a uniform ESMF grid spanning the resolved
  !> coordinate extents, so metadata consumers see the correct geometry
  !> without the cap duplicating coordinate derivation.
  subroutine InitializeRealize(comp, rc)
    type(ESMF_GridComp) :: comp
    integer, intent(out) :: rc

    type(ESMF_Grid) :: grid
    type(ESMF_VM) :: vm
    integer :: mpi_comm_val
    integer :: pet_count
    integer(c_int) :: c_rc
    integer(c_int) :: nx_c, ny_c, nz_c, topology_c
    real(c_double) :: lon_min, lon_max, lat_min, lat_max

    ! Parent-grid discovery state
    logical :: grid_is_present
    logical :: mesh_is_present
    type(ESMF_Grid) :: parent_grid
    type(ESMF_Mesh) :: parent_mesh
    integer :: gx_nx, gx_ny, gx_is_rad, gx_rc
    real(ESMF_KIND_R8), allocatable :: gx_lon(:), gx_lat(:)
    integer(c_int) :: nz_cfg
    integer(c_int) :: desc_len
    character(len=512) :: yaml_desc
    logical :: vm_ok
    character(len=700) :: wmsg

    rc = ESMF_SUCCESS
    call ESMF_LogWrite('[Cap] InitializeRealize entered', ESMF_LOGMSG_INFO)

    ! Retrieve the raw ESMF VM MPI communicator (same handle the C++ driver
    ! passes: an MPI_Comm_c2f value; 0 lets the facade default to
    ! MPI_COMM_WORLD). VM loss is not fatal here — the facade falls back to
    ! MPI_COMM_WORLD — so the standard rc check logs and recovers instead of
    ! bailing out.
    mpi_comm_val = 0
    vm_ok = .false.
    call ESMF_GridCompGet(comp, vm=vm, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      vm_ok = .false.
      mpi_comm_val = 0
      rc = ESMF_SUCCESS
    else
      call ESMF_VMGet(vm, mpiCommunicator=mpi_comm_val, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) then
        vm_ok = .false.
        mpi_comm_val = 0
        rc = ESMF_SUCCESS
      else
        vm_ok = .true.
      end if
    end if

    ! Resolve the grid source. A parent grid/mesh already associated with the
    ! component wins over the config-built YAML grid. Query the presence
    ! flags first: retrieving an absent grid or mesh would itself be an
    ! error, so the objects are only fetched when flagged present. A failed
    ! presence query is treated as absent (the config-grid path still works).
    grid_is_present = .false.
    mesh_is_present = .false.
    call ESMF_GridCompGet(comp, gridIsPresent=grid_is_present, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      grid_is_present = .false.
      rc = ESMF_SUCCESS
    end if
    call ESMF_GridCompGet(comp, meshIsPresent=mesh_is_present, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      mesh_is_present = .false.
      rc = ESMF_SUCCESS
    end if
    if (.not. grid_is_present .and. .not. mesh_is_present) then
      ! No host-supplied geometry: the grid comes from the CECE config. This
      ! is the supported standalone/testing configuration, but a coupled
      ! run reaching it usually means the connect failed silently, so say so.
      call ESMF_LogWrite('[Cap] No parent grid or mesh associated with the'// &
        ' component; resolving the target grid from the CECE config.', &
        ESMF_LOGMSG_WARNING)
    end if
    if (grid_is_present) then
      call ESMF_GridCompGet(comp, grid=parent_grid, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end if
    if (mesh_is_present) then
      call ESMF_GridCompGet(comp, mesh=parent_mesh, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end if

    if (grid_is_present .or. mesh_is_present) then
      ! Assembling the global coordinates is a collective operation on the
      ! component's VM, so the parent-grid path cannot proceed without it.
      if (.not. vm_ok) then
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
          msg='[Cap] Parent ESMF grid present but component VM unavailable;'// &
          ' cannot assemble global coordinates (no uniform-grid fallback).', &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
      ! Parent-provided grid path. The vertical layer count is always read
      ! from the config (driver.grid.nz): a flat 2-D grid carries no vertical
      ! dimension, so it can never supply nz.
      call cece_sim_nz_from_config(trim(g_config_file_path)//c_null_char, &
                                   int(len_trim(g_config_file_path), c_int), &
                                   nz_cfg, c_rc)
      if (c_rc /= 0 .or. nz_cfg <= 0) then
        write(wmsg, '(A,I0)') '[Cap] Could not resolve vertical layer count', &
          ' from config rc=', int(c_rc)
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if

      ! Precedence: when the YAML also describes a grid, name it as ignored
      ! (a single warning; the two sources are never merged). If the YAML
      ! grid cannot be derived at all there is nothing to warn about.
      yaml_desc = ' '
      desc_len = 0
      call cece_sim_describe_yaml_grid(trim(g_config_file_path)//c_null_char, &
                                       int(len_trim(g_config_file_path), c_int), &
                                       yaml_desc, int(len(yaml_desc), c_int), &
                                       desc_len, c_rc)
      if (c_rc == 0 .and. desc_len > 0) then
        if (desc_len <= len(yaml_desc)) then
          write(wmsg, '(A,A)') '[Cap] Parent ESMF grid takes precedence;', &
            ' ignoring the CECE config grid: ', yaml_desc(1:desc_len)
        else
          wmsg = '[Cap] Parent ESMF grid takes precedence; ignoring the CECE config grid.'
        end if
        call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_WARNING)
      end if

      ! Extract the global coordinate arrays across PETs. ESMF coordinate
      ! access is Fortran-only, so the cap does the gathering and hands the
      ! C++ facade plain arrays; normalization, topology classification and
      ! validation happen in shared C++ code. An unsupported parent
      ! topology fails here with a named diagnostic — no uniform-grid
      ! fallback is attempted.
      if (grid_is_present) then
        call cece_cap_extract_parent_grid(grid=parent_grid, vm=vm, &
             nx=gx_nx, ny=gx_ny, is_rad=gx_is_rad, &
             lon=gx_lon, lat=gx_lat, rc=gx_rc)
      else
        call cece_cap_extract_parent_grid(mesh=parent_mesh, vm=vm, &
             nx=gx_nx, ny=gx_ny, is_rad=gx_is_rad, &
             lon=gx_lon, lat=gx_lat, rc=gx_rc)
      end if
      if (gx_rc /= ESMF_SUCCESS) then
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
          msg='[Cap] Parent grid extraction failed; no fallback to a uniform grid.', &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if

      call cece_sim_create_from_esmf(trim(g_config_file_path)//c_null_char, &
                                     int(len_trim(g_config_file_path), c_int), &
                                     int(gx_nx, c_int), int(gx_ny, c_int), &
                                     nz_cfg, int(gx_is_rad, c_int), &
                                     gx_lon, int(size(gx_lon), c_int), &
                                     gx_lat, int(size(gx_lat), c_int), &
                                     int(mpi_comm_val, c_int), g_sim_ptr, c_rc)
      if (allocated(gx_lon)) deallocate(gx_lon)
      if (allocated(gx_lat)) deallocate(gx_lat)
      if (c_rc /= 0 .or. .not. c_associated(g_sim_ptr)) then
        write(wmsg, '(A,I0)') '[Cap] Failed to create CECE simulation on the', &
          ' parent-provided grid rc=', int(c_rc)
        g_sim_ptr = c_null_ptr
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if

      write(wmsg, '(A,I0,A,I0,A,I0)') '[Cap] Simulation grid from parent ESMF', &
        ' object: ', gx_nx, 'x', gx_ny, 'x', int(nz_cfg)
      call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_INFO)
      ! The component is already associated with the parent grid/mesh, so no
      ! re-association is needed on this path. Realize the configured exports
      ! on the parent geometry (a mesh-backed component is left unconnected:
      ! emission fields are grid fields, and offering an unsupported
      ! geometry fails rather than silently mis-coupling).
      call realize_export_fields(comp, parent_grid, grid_is_present, int(nz_cfg), rc)
      if (rc /= ESMF_SUCCESS) return  ! bail out
      call ESMF_LogWrite('[Cap] InitializeRealize completed successfully', &
        ESMF_LOGMSG_INFO)
      return
    end if

    ! Standalone case (no parent grid): build the simulation through the
    ! shared facade. Grid resolution (named grids, gridspec file,
    ! stream-inferred coordinates, uniform extents) runs inside C++ on the
    ! same code path as the standalone driver, together with core init,
    ! export-field registration, the driver orchestrator, and the output
    ! writer. If neither a parent grid nor a derivable YAML grid exists, the
    ! facade call fails loudly below — there is no silent substitution.
    call cece_sim_create_from_yaml(trim(g_config_file_path)//c_null_char, &
                                   int(len_trim(g_config_file_path), c_int), &
                                   int(mpi_comm_val, c_int), g_sim_ptr, c_rc)
    if (c_rc /= 0 .or. .not. c_associated(g_sim_ptr)) then
      write(wmsg, '(A,I0)') '[Cap] Failed to create CECE simulation rc=', int(c_rc)
      g_sim_ptr = c_null_ptr
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    ! Read the resolved grid back so the component can be associated with a
    ! matching ESMF grid. Rectilinear grids carry one row of ny cells;
    ! flattened curvilinear/unstructured grids carry ny == 1 with nx nodes.
    call cece_sim_grid_info(g_sim_ptr, nx_c, ny_c, nz_c, topology_c, &
                            lon_min, lon_max, lat_min, lat_max, c_rc)
    if (c_rc /= 0) then
      write(wmsg, '(A,I0)') '[Cap] Failed to read resolved grid info rc=', int(c_rc)
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    write(wmsg, '(A,I0,A,I0,A,I0,A,I0)') '[Cap] Simulation grid: ', nx_c, &
      'x', ny_c, 'x', nz_c, ' topology=', topology_c
    call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_INFO)

    ! Decompose the grid across the component's PETs by latitude rows only, so
    ! each rank's local slab matches the row band the shared core owns. The
    ! simulation partitions the global rows with the block formula
    !   band_start(r) = r*(ny/size) + min(r, ny%size),
    ! giving the first ny%size ranks one extra row. ESMF's balanced division of
    ! a single dimension into DEs follows the same convention, so splitting the
    ! latitude dimension into petCount balanced tiles and leaving longitude
    ! whole reproduces the band geometry exactly at any PET count. Without this
    ! the default column-first split hands each rank a different footprint than
    ! its band and the export binding fails the extent check.
    pet_count = 1
    if (vm_ok) then
      call ESMF_VMGet(vm, petCount=pet_count, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end if
    if (pet_count < 1) pet_count = 1

    grid = ESMF_GridCreateNoPeriDimUfrm(maxIndex=(/nx_c, ny_c/), &
      minCornerCoord=(/lon_min, lat_min/), &
      maxCornerCoord=(/lon_max, lat_max/), &
      regDecomp=(/1, pet_count/), &
      coordSys=ESMF_COORDSYS_SPH_DEG, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_GridCompSet(comp, grid=grid, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! The grid stays associated with the component after this routine
    ! returns, so it must NOT be destroyed here: ESMF's allocation rule is
    ! that items associated with a component are not destroyed with it, and
    ! the component keeps using this grid until the framework tears it down.
    ! Consistent with the NUOPC model templates, the grid handle is released
    ! with the component at ESMF_Finalize rather than manually.
    call realize_export_fields(comp, grid, .true., nz_c, rc)
    if (rc /= ESMF_SUCCESS) return  ! bail out

    call ESMF_LogWrite('[Cap] InitializeRealize completed successfully', &
      ESMF_LOGMSG_INFO)
  end subroutine InitializeRealize

  !> @brief Realize the configured NUOPC export fields on the component grid.
  !>
  !> Shared by both grid paths of InitializeRealize (parent-provided grid and
  !> config-built uniform grid). For every species listed in the `nuopc:`
  !> export section — the same alphabetical order advertised in
  !> InitializeAdvertise — a connected field gets real storage on this
  !> component's grid: an ESMF grid field with the ungridded vertical running
  !> 1:nz (matching the consumer's realization), handed to NUOPC_Realize to
  !> replace the advertised placeholder, then bound into the simulation so
  !> each step's write-back lands directly in ESMF-owned memory (reference
  !> sharing: no gather, no copy through the cap).
  !>
  !> A field the driver did not connect is removed from the export state, so
  !> nothing is allocated for it and no unmatched item survives realization;
  !> the removal is logged at INFO.
  !>
  !> `has_grid` is false only for a mesh-backed component: emission fields
  !> are grid fields, so every export is reported as an unconnected removal
  !> rather than mis-coupled onto an unsupported geometry.
  subroutine realize_export_fields(comp, grid, has_grid, nz, rc)
    type(ESMF_GridComp)      :: comp
    type(ESMF_Grid)          :: grid
    logical,    intent(in)   :: has_grid
    integer(c_int), intent(in) :: nz
    integer,    intent(out)  :: rc

    type(ESMF_State) :: exportState
    integer :: i, nexp
    integer(c_int) :: count_c, c_rc
    integer(c_int) :: species_len, std_len, units_len, name_len
    character(kind=c_char), dimension(ESMF_MAXSTR) :: c_species, c_std, c_units, c_name
    character(len=ESMF_MAXSTR) :: key, item_name
    character(len=700) :: msg
    logical :: connected
    type(ESMF_Field) :: field
    real(c_double), pointer :: fptr(:, :, :)
    integer :: al(3), au(3)
    integer :: fnx, fny, fnz
    type(c_ptr) :: farray_ptr

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(comp, exportState=exportState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call cece_nuopc_export_count(trim(g_config_file_path)//c_null_char, &
                                 int(len_trim(g_config_file_path), c_int), &
                                 count_c, c_rc)
    if (c_rc /= 0) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[Cap] Failed to count configured NUOPC export fields at realize', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if
    nexp = int(count_c)

    do i = 0, nexp - 1
      call cece_nuopc_export_spec(trim(g_config_file_path)//c_null_char, &
        int(len_trim(g_config_file_path), c_int), int(i, c_int), &
        c_species, int(ESMF_MAXSTR, c_int), species_len, &
        c_std, int(ESMF_MAXSTR, c_int), std_len, &
        c_units, int(ESMF_MAXSTR, c_int), units_len, &
        c_name, int(ESMF_MAXSTR, c_int), name_len, c_rc)
      if (c_rc /= 0) then
        write(msg, '(A,I0)') '[Cap] Failed to read NUOPC export spec at index ', i
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
      key = nuopc_cbuf_to_string(c_species, int(species_len))
      ! The state item name defaults to the species key unless the config
      ! supplies an explicit name (identical rule to the advertise phase).
      if (name_len > 0) then
        item_name = nuopc_cbuf_to_string(c_name, int(name_len))
      else
        item_name = key
      end if

      connected = .false.
      if (has_grid) then
        connected = NUOPC_IsConnected(exportState, fieldName=trim(item_name), rc=rc)
        if (rc /= ESMF_SUCCESS) then
          ! An absent item simply means nothing was advertised under this
          ! name (the advertise phase ran from the same list, so this cannot
          ! happen for a configured field); treat it as not connected.
          rc = ESMF_SUCCESS
          connected = .false.
        end if
      end if

      if (.not. connected) then
        call ESMF_StateRemove(exportState, (/trim(item_name)/), rc=rc)
        if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
          line=__LINE__, file=__FILE__)) return  ! bail out
        write(msg, '(A,A,A)') "[Cap] Removed unconnected export field '", &
          trim(item_name), "'"
        call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
        cycle
      end if

      ! Connected: allocate real storage on the component grid. The grid
      ! covers two field dimensions (gridToFieldMap), and the vertical is an
      ! ungridded dimension spanning 1:nz — the same shape the consumer
      ! realizes on its side, which is what the field transfer matches on.
      field = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, &
        staggerloc=ESMF_STAGGERLOC_CENTER, &
        gridToFieldMap=(/1, 2/), &
        ungriddedLBound=(/1/), ungriddedUBound=(/int(nz)/), &
        name=trim(item_name), rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      call NUOPC_Realize(exportState, field=field, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      ! Hand the field's own memory to the simulation: from now on each
      ! step's write-back deep-copies the computed band directly into it.
      ! ESMF associates the pointer with the field's per-PET storage, bounds
      ! included; the extents read back here are what the simulation verifies
      ! against its own band decomposition before rebinding the write-back.
      nullify(fptr)
      call ESMF_FieldGet(field, farrayPtr=fptr, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
      al = lbound(fptr)
      au = ubound(fptr)
      fnx = au(1) - al(1) + 1
      fny = au(2) - al(2) + 1
      fnz = au(3) - al(3) + 1

      if (fnx <= 0 .or. fny <= 0 .or. fnz <= 0) then
        ! This PET owns no rows of the field; there is nothing to write back
        ! and nothing to bind. Skip rather than hand the core a null target.
        write(msg, '(A,A,A,I0,A,I0,A,I0,A)') '[Cap] Export field "', &
          trim(item_name), '" has empty storage on this PET (', &
          fnx, 'x', fny, 'x', fnz, '); bind skipped'
        call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
        cycle
      end if

      farray_ptr = C_LOC(fptr)
      call cece_sim_bind_export_field(g_sim_ptr, key//c_null_char, &
                                      int(len_trim(key), c_int), farray_ptr, &
                                      int(fnx, c_int), int(fny, c_int), &
                                      int(fnz, c_int), c_rc)
      if (c_rc /= 0) then
        write(msg, '(A,A,A,I0)') '[Cap] Failed to bind export field "', &
          trim(item_name), '" to simulation storage rc=', int(c_rc)
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if

      write(msg, '(A,A,A,I0,A,I0,A,I0,A)') '[Cap] Realized export field "', &
        trim(item_name), '" (', fnx, 'x', fny, 'x', fnz, ') bound to simulation'
      call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
    end do
  end subroutine realize_export_fields

  !> @brief Realize the accepted host-provided imports on the transferred grid.
  !>
  !> Runs in the accepted phase, after the framework has moved each connected
  !> provider's geometry into CECE's import state. For every configured import
  !> the same alphabetical list advertised in InitializeAdvertise: a connected
  !> field is realized as a rank-2 R8 surface field on the transferred grid
  !> (gridToFieldMap defaults to all grid dimensions, no ungridded vertical),
  !> and its state-item name is cached so each Run can copy the delivered host
  !> storage into the simulation. An import the host did not connect is removed
  !> from the state and logged; CECE then falls back to file ingest for that
  !> name, so the run proceeds unchanged.
  subroutine RealizeAccepted(comp, rc)
    type(ESMF_GridComp)      :: comp
    integer,    intent(out)  :: rc

    type(ESMF_State) :: importState
    integer :: i, nimp, nkept
    integer(c_int) :: count_c, c_rc
    integer(c_int) :: field_len, std_len, units_len, name_len
    character(kind=c_char), dimension(ESMF_MAXSTR) :: c_field, c_std, c_units, c_name
    character(len=ESMF_MAXSTR) :: key, item_name
    character(len=700) :: msg
    logical :: connected

    rc = ESMF_SUCCESS
    call ESMF_LogWrite('[Cap] RealizeAccepted entered', ESMF_LOGMSG_INFO)

    call NUOPC_ModelGet(comp, importState=importState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Reset the cached connected-import list (idempotent across re-realize).
    if (allocated(g_import_names)) deallocate(g_import_names)
    allocate(g_import_names(0))

    call cece_nuopc_import_count(trim(g_config_file_path)//c_null_char, &
                                 int(len_trim(g_config_file_path), c_int), &
                                 count_c, c_rc)
    if (c_rc /= 0) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[Cap] Failed to count configured NUOPC import fields at realize', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if
    nimp = int(count_c)

    nkept = 0
    do i = 0, nimp - 1
      call cece_nuopc_import_spec(trim(g_config_file_path)//c_null_char, &
        int(len_trim(g_config_file_path), c_int), int(i, c_int), &
        c_field, int(ESMF_MAXSTR, c_int), field_len, &
        c_std, int(ESMF_MAXSTR, c_int), std_len, &
        c_units, int(ESMF_MAXSTR, c_int), units_len, &
        c_name, int(ESMF_MAXSTR, c_int), name_len, c_rc)
      if (c_rc /= 0) then
        write(msg, '(A,I0)') '[Cap] Failed to read NUOPC import spec at index ', i
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(msg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return  ! bail out
      end if
      key = nuopc_cbuf_to_string(c_field, int(field_len))
      ! The state item name defaults to the configured input key unless the
      ! config supplies an explicit name (identical rule to the advertise).
      if (name_len > 0) then
        item_name = nuopc_cbuf_to_string(c_name, int(name_len))
      else
        item_name = key
      end if

      connected = NUOPC_IsConnected(importState, fieldName=trim(item_name), rc=rc)
      if (rc /= ESMF_SUCCESS) then
        ! An absent item means nothing was advertised under this name; treat
        ! it as not connected rather than failing.
        rc = ESMF_SUCCESS
        connected = .false.
      end if

      if (.not. connected) then
        ! Realize with removeNotConnected so the placeholder is dropped and no
        ! unmatched item survives realization; the transfer overload is the
        ! documented way to prune an accepted-but-unconnected import.
        call NUOPC_Realize(importState, fieldName=trim(item_name), &
          typekind=ESMF_TYPEKIND_R8, &
          realizeOnlyConnected=.true., removeNotConnected=.true., rc=rc)
        if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
          line=__LINE__, file=__FILE__)) return  ! bail out
        write(msg, '(A,A,A)') "[Cap] Removed unconnected import field '", &
          trim(item_name), "'"
        call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
        cycle
      end if

      ! Connected: allocate the surface field on the transferred grid. A met
      ! import is rank-2 with the grid's two dimensions mapping to the field
      ! and no ungridded vertical (gridToFieldMap and ungridded bounds omitted).
      call NUOPC_Realize(importState, fieldName=trim(item_name), &
        typekind=ESMF_TYPEKIND_R8, &
        realizeOnlyConnected=.true., removeNotConnected=.true., rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      ! Cache the name so Run copies this field's storage each step.
      block
        character(len=ESMF_MAXSTR), allocatable :: tmp(:)
        integer :: n
        n = nkept + 1
        allocate(tmp(n))
        if (nkept > 0) tmp(1:nkept) = g_import_names(1:nkept)
        call move_alloc(tmp, g_import_names)
      end block
      nkept = nkept + 1
      g_import_names(nkept) = item_name

      write(msg, '(A,A,A)') '[Cap] Realized import field ''', &
        trim(item_name), ''' on the transferred grid'
      call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
    end do

    write(msg, '(A,I0,A,I0,A)') '[Cap] Realized ', nkept, ' of ', nimp, &
      ' NUOPC import field(s)'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
  end subroutine RealizeAccepted

  !> @brief Run Advance step (Specialized via model_label_Advance)
  !>
  !> Drives one step of the shared simulation. The NUOPC clock's currTime
  !> is the step START instant (observed: the first Advance sees
  !> currTime == clock start), and the step END instant is the clock's
  !> next time. The shared core ingests at step start, computes, and stamps
  !> output at step end; completion is honored via the shared complete
  !> signal, so both drivers take identical step counts.
  subroutine Run(comp, rc)
    type(ESMF_GridComp) :: comp
    integer, intent(out) :: rc

    type(ESMF_Clock) :: clock
    type(ESMF_Time) :: currTime
    type(ESMF_Time) :: nextTime
    character(len=64) :: step_start_str, step_end_str
    character(len=700) :: wmsg
    integer(c_int) :: complete_c
    integer(c_int) :: c_rc
    integer :: i
    type(ESMF_State) :: importState
    type(ESMF_Field) :: import_field
    real(c_double), pointer :: fptr(:, :)
    integer :: al(2), au(2)
    integer :: fnx, fny
    type(c_ptr) :: farray_ptr

    rc = ESMF_SUCCESS

    ! Once the shared core reports completion, the host must stop stepping:
    ! do no further work on later advances (the harness may still call).
    if (g_complete) then
      call ESMF_LogWrite('[Cap] Simulation already complete; skipping advance', &
        ESMF_LOGMSG_INFO)
      return
    end if

    if (.not. c_associated(g_sim_ptr)) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[Cap] No live simulation at advance; was Realize skipped?', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    ! Query the Model for its clock through the NUOPC interface (the
    ! canonical Model Advance pattern), not the raw ESMF component getter.
    call NUOPC_ModelGet(comp, modelClock=clock, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_ClockGet(clock, currTime=currTime, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_ClockGetNextTime(clock, nextTime, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_TimeGet(currTime, timeString=step_start_str, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Remember the step start for the export stamping specialization that
    ! the framework invokes at the end of this Run.
    g_step_start_time = currTime
    call ESMF_TimeGet(nextTime, timeString=step_end_str, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! The writer counts steps 1-based (output frequency is checked as
    ! step_index % output_freq), matching the standalone driver's counter.
    g_step_count = g_step_count + 1

    ! Copy each connected host-provided import into the simulation before the
    ! step, so this step's compute reads the delivered values. The Connector
    ! has already written the provider's data into the field's own storage on
    ! the transferred grid; the cap only hands that borrowed pointer and the
    ! field's per-PET extents to the facade, which resolves the configured
    ! input name to the import-state key and copies it in.
    if (allocated(g_import_names)) then
      if (size(g_import_names) > 0) then
        call NUOPC_ModelGet(comp, importState=importState, rc=rc)
        if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
          line=__LINE__, file=__FILE__)) return  ! bail out

        do i = 1, size(g_import_names)
          call ESMF_StateGet(importState, trim(g_import_names(i)), &
                             import_field, rc=rc)
          if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
            line=__LINE__, file=__FILE__)) return  ! bail out

          nullify(fptr)
          call ESMF_FieldGet(import_field, farrayPtr=fptr, rc=rc)
          if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
            line=__LINE__, file=__FILE__)) return  ! bail out

          al = lbound(fptr)
          au = ubound(fptr)
          fnx = au(1) - al(1) + 1
          fny = au(2) - al(2) + 1

          ! A PET that owns no rows has an empty local slab; there is nothing
          ! to copy. The facade still needs the shape, so skip the call rather
          ! than hand it a zero-extent or null pointer.
          if (fnx <= 0 .or. fny <= 0 .or. .not. associated(fptr)) then
            write(wmsg, '(A,A,A,I0,A,I0,A)') '[Cap] Import field "', &
              trim(g_import_names(i)), '" empty on this PET (', &
              fnx, 'x', fny, '); copy skipped'
            call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_INFO)
            cycle
          end if

          farray_ptr = C_LOC(fptr)
          call cece_sim_set_import_field(g_sim_ptr, &
              g_import_names(i)//c_null_char, &
              int(len_trim(g_import_names(i)), c_int), farray_ptr, &
              int(fnx, c_int), int(fny, c_int), c_rc)
          if (c_rc /= 0) then
            write(wmsg, '(A,A,A,I0)') '[Cap] Failed to copy import field "', &
              trim(g_import_names(i)), '" into simulation rc=', int(c_rc)
            call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
              line=__LINE__, file=__FILE__, rcToReturn=rc)
            return  ! bail out
          end if

          write(wmsg, '(A,A,A,I0,A,I0,A)') "[Cap] Copied import field '", &
            trim(g_import_names(i)), "' (", fnx, 'x', fny, ') into simulation'
          call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_INFO)
        end do
      end if
    end if

    call cece_sim_step(g_sim_ptr, trim(step_start_str)//c_null_char, &
                       int(len_trim(step_start_str), c_int), &
                       trim(step_end_str)//c_null_char, &
                       int(len_trim(step_end_str), c_int), &
                       int(g_step_count, c_int), complete_c, c_rc)
    if (c_rc < 0) then
      write(wmsg, '(A,I0)') '[Cap] cece_sim_step failed rc=', int(c_rc)
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return  ! bail out
    end if

    if (complete_c /= 0) then
      g_complete = .true.
      call ESMF_LogWrite('[Cap] Shared core reported simulation completion', &
        ESMF_LOGMSG_INFO)
    end if
  end subroutine Run

  !> @brief Stamp export fields at the step-start instant (Specialized via
  !> label_TimestampExport, replacing the framework default).
  !>
  !> The framework invokes this after the model's internal step loop, at which
  !> point the default would stamp with the clock's step-end time. Coupled
  !> consumers compare import timestamps against their clock currTime, which
  !> during a driver sweep equals the step start; with reference-shared fields
  !> the consumer sees the provider's live stamp directly, so the step-end
  !> default is always one step ahead of the check and fails it. Stamping the
  !> step start aligns the shared export with every consumer sweep, and the
  !> value semantically covers the interval beginning at that instant.
  subroutine StampExports(comp, rc)
    type(ESMF_GridComp) :: comp
    integer, intent(out) :: rc

    type(ESMF_State) :: exportState

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(comp, exportState=exportState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Unconnected fields were pruned from the export state at realization,
    ! so only coupled exports are stamped here.
    call NUOPC_SetTimestamp(exportState, g_step_start_time, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out
  end subroutine StampExports

  !> @brief Finalize and cleanup resources (Specialized via model_label_Finalize)
  subroutine Finalize(comp, rc)
    type(ESMF_GridComp) :: comp
    integer, intent(out) :: rc

    integer(c_int) :: c_rc
    character(len=700) :: wmsg

    rc = ESMF_SUCCESS
    call ESMF_LogWrite('[Cap] Finalizing CECE NUOPC Cap...', ESMF_LOGMSG_INFO)

    ! Tear down the shared simulation: driver orchestrator destroy, then
    ! core finalize. Non-zero rc is a warning only — output has already
    ! been flushed — matching the standalone driver's teardown semantics.
    call cece_sim_finalize(g_sim_ptr, c_rc)
    if (c_rc /= 0) then
      write(wmsg, '(A,I0)') '[Cap] CECE teardown reported failures (rc=', int(c_rc)
      call ESMF_LogWrite(trim(wmsg), ESMF_LOGMSG_WARNING)
    end if
    g_sim_ptr = c_null_ptr

    call ESMF_LogWrite('[Cap] CECE NUOPC Cap finalized successfully', &
      ESMF_LOGMSG_INFO)
  end subroutine Finalize

end module cece_cap_mod
