!> @file cece_sink_mod.F90
!> @brief Minimal NUOPC Model peer that receives CECE's emission exports and
!> writes them to NetCDF, for the field-coupling integration tests.
!>
!> The sink is the acceptor side of a CECE -> sink connection. It advertises
!> one import for every CECE export standard name, accepting the geometry the
!> provider offers (TransferOfferGeomObject="cannot provide"), and realizes
!> those imports on the transferred grid with the same 3-D-on-2-D shape CECE
!> exports (gridToFieldMap=(/1,2/), ungridded 1:nz). Because the sink shares
!> CECE's decomposition and both sides set SharePolicy=share, the NUOPC
!> Connector reference-shares the arrays (NUOPC refdoc 2.4.8): the sink's
!> import fields alias CECE's export buffers in place, with no copies and no
!> gather to a single PET.
!>
!> Each Advance writes every connected import field with ESMF_FieldWrite, which
!> assembles the distributed field into one global file across the component's
!> PETs. The sink never touches a whole-field buffer itself; consolidating the
!> output is ESMF's job, exactly as it is for CECE's own file writer.
!>
!> The list of fields (state-item name, standard name, units) and the vertical
!> layer count are supplied by the driver application through the public
!> setters before the component is added, read from the same CECE config the
!> cap uses. This keeps the sink in lock-step with CECE's advertisement without
!> duplicating any parsing.
module cece_sink_mod

  use ESMF
  use NUOPC
  use NUOPC_Model, modelSS => SetServices
  use NUOPC_Model, only: &
    label_Advertise, &
    label_RealizeProvided, &
    label_RealizeAccepted, &
    model_label_Advance => label_Advance, &
    model_label_Finalize => label_Finalize

  implicit none

  private

  public :: CECE_SINK_SetServices
  public :: sink_begin_fields, sink_add_import_field, sink_end_fields
  public :: sink_set_vertical_layers, sink_set_output_dir
  public :: sink_has_fields

  !> @brief Number of import fields the sink advertises (set by the app).
  integer, save :: g_nfields = 0

  !> @brief State-item name per field (matches CECE's export item name, so the
  !> written NetCDF variable name lines up with the standalone output).
  character(len=ESMF_MAXSTR), allocatable, save :: g_names(:)

  !> @brief Standard name per field (must equal CECE's export standard name).
  character(len=ESMF_MAXSTR), allocatable, save :: g_stdnames(:)

  !> @brief Units per field (empty string => omit, use dictionary canonical).
  character(len=ESMF_MAXSTR), allocatable, save :: g_units(:)

  !> @brief Vertical layer count for the 3-D exports (from the CECE config).
  integer, save :: g_nz = 1

  !> @brief Directory the per-step sink NetCDF files are written into.
  character(len=ESMF_MAXSTR), save :: g_outdir = "."

  !> @brief Monotonic Advance counter, used to name the per-step output file.
  integer, save :: g_step = 0

  !> @brief Scratch during field registration before the arrays are sized.
  character(len=ESMF_MAXSTR), save :: g_stage_name
  character(len=ESMF_MAXSTR), save :: g_stage_std
  character(len=ESMF_MAXSTR), save :: g_stage_units

contains

  !> @brief Reset the pending field list (call before the first add).
  subroutine sink_begin_fields()
    g_nfields = 0
    if (allocated(g_names)) deallocate(g_names)
    if (allocated(g_stdnames)) deallocate(g_stdnames)
    if (allocated(g_units)) deallocate(g_units)
    allocate(g_names(0), g_stdnames(0), g_units(0))
  end subroutine sink_begin_fields

  !> @brief Queue one import field; committed by sink_end_fields.
  !!
  !! Stored in staging globals so the caller can pass plain strings; the app
  !! calls this once per CECE export, in the same alphabetical order the cap
  !! advertises, then sink_end_fields.
  subroutine sink_add_import_field(name, standard_name, units)
    character(len=*), intent(in) :: name, standard_name
    character(len=*), intent(in) :: units
    g_stage_name = name
    g_stage_std = standard_name
    g_stage_units = units
  end subroutine sink_add_import_field

  !> @brief Append the staged field to the registered list.
  subroutine sink_end_fields()
    integer :: n
    n = g_nfields + 1
    g_nfields = n
    block
      character(len=ESMF_MAXSTR), allocatable :: tmp(:)
      allocate(tmp(n))
      tmp(1:g_nfields - 1) = g_names(1:g_nfields - 1)
      call move_alloc(tmp, g_names)
    end block
    block
      character(len=ESMF_MAXSTR), allocatable :: tmp(:)
      allocate(tmp(n))
      tmp(1:g_nfields - 1) = g_stdnames(1:g_nfields - 1)
      call move_alloc(tmp, g_stdnames)
    end block
    block
      character(len=ESMF_MAXSTR), allocatable :: tmp(:)
      allocate(tmp(n))
      tmp(1:g_nfields - 1) = g_units(1:g_nfields - 1)
      call move_alloc(tmp, g_units)
    end block
    g_names(n) = g_stage_name
    g_stdnames(n) = g_stage_std
    g_units(n) = g_stage_units
  end subroutine sink_end_fields

  !> @brief Set the vertical layer count the 3-D exports carry.
  subroutine sink_set_vertical_layers(nz)
    integer, intent(in) :: nz
    g_nz = nz
  end subroutine sink_set_vertical_layers

  !> @brief True when at least one import field has been registered.
  function sink_has_fields() result(any_fields)
    logical :: any_fields
    any_fields = (g_nfields > 0)
  end function sink_has_fields

  !> @brief Set the output directory for the per-step sink files.
  subroutine sink_set_output_dir(dir)
    character(len=*), intent(in) :: dir
    g_outdir = dir
  end subroutine sink_set_output_dir

  !> @brief SetServices for the sink component.
  subroutine CECE_SINK_SetServices(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    rc = ESMF_SUCCESS

    call NUOPC_CompDerive(gcomp, modelSS, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_CompSpecialize(gcomp, specLabel=label_Advertise, &
      specRoutine=Advertise, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! The sink provides no export fields, but the provider-realize phase must
    ! still be specialized; it is an intentional no-op.
    call NUOPC_CompSpecialize(gcomp, specLabel=label_RealizeProvided, &
      specRoutine=RealizeProvided, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Imports accept the transferred geometry, so they realize in the accepted
    ! phase (not the provided one).
    call NUOPC_CompSpecialize(gcomp, specLabel=label_RealizeAccepted, &
      specRoutine=RealizeAccepted, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Advance, &
      specRoutine=Advance, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Finalize, &
      specRoutine=Finalize, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out
  end subroutine CECE_SINK_SetServices

  !> @brief Advertise one import per CECE export, accepting the geometry.
  subroutine Advertise(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    type(ESMF_State) :: importState
    integer :: i
    character(len=ESMF_MAXSTR) :: msg

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(gcomp, importState=importState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Reference sharing must be requested on BOTH sides: NUOPC_Advertise
    ! defaults the share policies to "not share", which would make the
    ! Connector regrid instead of aliasing CECE's storage.
    do i = 1, g_nfields
      if (len_trim(g_units(i)) > 0) then
        call NUOPC_Advertise(importState, &
          StandardName=trim(g_stdnames(i)), name=trim(g_names(i)), &
          Units=trim(g_units(i)), &
          TransferOfferGeomObject="cannot provide", &
          SharePolicyField="share", SharePolicyGeomObject="share", rc=rc)
      else
        call NUOPC_Advertise(importState, &
          StandardName=trim(g_stdnames(i)), name=trim(g_names(i)), &
          TransferOfferGeomObject="cannot provide", &
          SharePolicyField="share", SharePolicyGeomObject="share", rc=rc)
      end if
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end do

    write(msg, '(A,I0,A)') '[Sink] Advertised ', g_nfields, &
      ' import field(s), accepting transferred geometry'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
  end subroutine Advertise

  !> @brief No-op: the sink owns no export fields.
  subroutine RealizeProvided(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc
    rc = ESMF_SUCCESS
  end subroutine RealizeProvided

  !> @brief Realize the accepted imports on the transferred grid.
  !!
  !! Each connected import is realized with the same shape CECE exports: two
  !! grid dimensions plus one ungridded vertical dimension spanning 1:nz. The
  !! grid comes from the transfer, so the decomposition matches CECE's band and
  !! the Connector can reference-share the arrays.
  subroutine RealizeAccepted(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    type(ESMF_State) :: importState
    integer :: i
    integer :: gridToFieldMap(2)
    integer :: ungriddedLBound(1)
    integer :: ungriddedUBound(1)
    character(len=ESMF_MAXSTR) :: msg

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(gcomp, importState=importState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    gridToFieldMap = (/1, 2/)
    ungriddedLBound = (/1/)
    ungriddedUBound = (/g_nz/)

    do i = 1, g_nfields
      call NUOPC_Realize(importState, &
        fieldName=trim(g_names(i)), &
        typekind=ESMF_TYPEKIND_R8, &
        gridToFieldMap=gridToFieldMap, &
        ungriddedLBound=ungriddedLBound, &
        ungriddedUBound=ungriddedUBound, &
        realizeOnlyConnected=.true., &
        removeNotConnected=.true., rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end do

    write(msg, '(A,I0,A)') '[Sink] Realized ', g_nfields, &
      ' accepted import field(s) on the transferred grid'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
  end subroutine RealizeAccepted

  !> @brief Write every connected import field to this step's sink file.
  subroutine Advance(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    type(ESMF_State) :: importState
    type(ESMF_Field) :: field
    integer :: i
    character(len=ESMF_MAXSTR) :: fname, msg
    logical :: connected

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(gcomp, importState=importState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    g_step = g_step + 1

    do i = 1, g_nfields
      connected = NUOPC_IsConnected(importState, fieldName=trim(g_names(i)), rc=rc)
      if (rc /= ESMF_SUCCESS) then
        ! A name absent from the state (never connected) is not an error here;
        ! reset and skip.
        rc = ESMF_SUCCESS
        cycle
      end if
      if (.not. connected) cycle

      call ESMF_StateGet(importState, trim(g_names(i)), field, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      write(fname, '(A,A,A,A,I0,A)') trim(g_outdir), '/sink_', &
        trim(g_names(i)), '_', g_step, '.nc'
      ! ESMF's PIO layer assembles the distributed field into one global file
      ! across the component's PETs. The sink never touches a whole-field
      ! buffer itself.
      call ESMF_FieldWrite(field, fileName=trim(fname), &
        variableName=trim(g_names(i)), overwrite=.true., rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
    end do

    write(msg, '(A,I0,A)') '[Sink] Advance wrote fields for step ', g_step
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
  end subroutine Advance

  !> @brief Finalize: nothing to release (the arrays belong to CECE).
  subroutine Finalize(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc
    rc = ESMF_SUCCESS
    call ESMF_LogWrite('[Sink] Finalized', ESMF_LOGMSG_INFO)
  end subroutine Finalize

end module cece_sink_mod
