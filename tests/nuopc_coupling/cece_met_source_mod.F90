!> @file cece_met_source_mod.F90
!> @brief Minimal NUOPC Model peer that supplies a constant meteorology field
!> to CECE, for the import-coupling integration test.
!>
!> The met source is the provider side of a met-source -> CECE connection. It
!> owns a uniform latitude-longitude grid matching the coupled fixture and
!> advertises one export (air temperature) offering its own geometry
!> ("will provide") with reference sharing requested on its side. Because the
!> grid geometry and row decomposition are identical to CECE's, the NUOPC
!> Connector aliases this export's storage directly into CECE's import
!> (no FieldBundle, so no regrid and no coordinate interpolation).
!>
!> Each Advance fills the export field's own storage with a constant value
!> through its farrayPtr. The export timestamp is left to the framework default
!> (step end); the run sequence runs the met -> CECE connector before this
!> component advances, so CECE receives the previous step-end stamp, which
!> equals its own step start and satisfies the default import-time check.
!>
!> The grid geometry mirrors the coupled fixture (a 72x36 global regular
!> lat-lon) and the decomposition splits latitude rows across PETs the same
!> way CECE's bands do, so the two components share identical data
!> distributions at any PET count — the precondition for reference sharing.
module cece_met_source_mod

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

  public :: CECE_MET_SOURCE_SetServices
  public :: met_source_set_value

  !> @brief Standard name, state-item name, and units of the single export.
  character(len=*), parameter :: g_std_name = "air_temperature"
  character(len=*), parameter :: g_item_name = "temperature"
  character(len=*), parameter :: g_units = "K"

  !> @brief Constant value written into the export field every Advance.
  real(esmf_kind_r8), save :: g_value = 310.0_esmf_kind_r8

  !> @brief Uniform grid geometry mirroring the coupled fixture. Longitude is
  !> whole; latitude is split into one balanced tile per PET, matching the
  !> row-band decomposition CECE uses so the connector copy needs no remap.
  integer, save :: g_nx = 72
  integer, save :: g_ny = 36
  real(esmf_kind_r8), save :: g_lon_min = -180.0_esmf_kind_r8
  real(esmf_kind_r8), save :: g_lon_max = 180.0_esmf_kind_r8
  real(esmf_kind_r8), save :: g_lat_min = -90.0_esmf_kind_r8
  real(esmf_kind_r8), save :: g_lat_max = 90.0_esmf_kind_r8

  !> @brief Monotonic Advance counter, used only to label the log message.
  integer, save :: g_step = 0

contains

  !> @brief Override the constant forcing value (default 310 K).
  subroutine met_source_set_value(value)
    real(esmf_kind_r8), intent(in) :: value
    g_value = value
  end subroutine met_source_set_value

  !> @brief SetServices for the met source component.
  subroutine CECE_MET_SOURCE_SetServices(gcomp, rc)
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

    ! The met source only provides an export; it owns the geometry, so the
    ! field is realized in the provided phase on its own grid.
    call NUOPC_CompSpecialize(gcomp, specLabel=label_RealizeProvided, &
      specRoutine=RealizeProvided, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Advance, &
      specRoutine=Advance, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! The export timestamp is left to the framework default (step-end), and no
    ! TimestampExport specialization is registered. The run sequence executes
    ! the met -> CECE connector BEFORE this component advances, so CECE reads
    ! the met source's PREVIOUS step-end stamp, which equals CECE's own step
    ! start and satisfies the consumer's default import-time check. This is the
    ! reference AtmOcnProto convention: a provider that runs after its outgoing
    ! connector needs no user-side stamping at all.

    call NUOPC_CompSpecialize(gcomp, specLabel=model_label_Finalize, &
      specRoutine=Finalize, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out
  end subroutine CECE_MET_SOURCE_SetServices

  !> @brief Advertise the single export, offering this component's geometry.
  subroutine Advertise(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    type(ESMF_State) :: exportState
    character(len=ESMF_MAXSTR) :: msg

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(gcomp, exportState=exportState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! "will provide" with reference sharing requested. NUOPC_Advertise
    ! defaults the share policies to "not share", which would make the
    ! Connector regrid the grid instead of aliasing storage; sharing is
    ! requested explicitly so the identical-geometry import is a direct alias.
    call NUOPC_Advertise(exportState, StandardName=trim(g_std_name), &
      name=trim(g_item_name), Units=trim(g_units), &
      TransferOfferGeomObject='will provide', &
      SharePolicyField='share', SharePolicyGeomObject='share', rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    msg = '[MetSource] Advertised export "' // trim(g_item_name) // &
      '" (' // trim(g_std_name) // ')'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
  end subroutine Advertise

  !> @brief Build the uniform grid and realize the export field on it.
  subroutine RealizeProvided(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    type(ESMF_State) :: exportState
    type(ESMF_Grid) :: grid
    type(ESMF_Field) :: field
    type(ESMF_VM) :: vm
    integer :: pet_count
    character(len=ESMF_MAXSTR) :: msg

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(gcomp, exportState=exportState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Split latitude across the component's PETs so each rank's local slab
    ! matches the row band CECE owns; the connector then copies band-to-band
    ! with no remap. ESMF's balanced division of one dimension matches the
    ! band formula CECE uses, so petCount tiles reproduce the geometry.
    pet_count = 1
    call ESMF_GridCompGet(gcomp, vm=vm, rc=rc)
    if (rc == ESMF_SUCCESS) then
      call ESMF_VMGet(vm, petCount=pet_count, rc=rc)
      if (rc /= ESMF_SUCCESS) pet_count = 1
      rc = ESMF_SUCCESS
    end if
    if (pet_count < 1) pet_count = 1

    grid = ESMF_GridCreateNoPeriDimUfrm(maxIndex=(/g_nx, g_ny/), &
      minCornerCoord=(/g_lon_min, g_lat_min/), &
      maxCornerCoord=(/g_lon_max, g_lat_max/), &
      regDecomp=(/1, pet_count/), &
      coordSys=ESMF_COORDSYS_SPH_DEG, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Reference sharing means the Connector never converts this grid to a
    ! mesh, so no corner coordinates are needed: the center-stagger
    ! coordinates the uniform-grid constructor already filled are sufficient.
    call ESMF_GridCompSet(gcomp, grid=grid, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! Plain 2-D grid field (both grid dimensions map to the field, no
    ! ungridded vertical) — the shape CECE realizes its 2-D met import to.
    field = ESMF_FieldCreate(grid, typekind=ESMF_TYPEKIND_R8, &
      staggerloc=ESMF_STAGGERLOC_CENTER, name=trim(g_item_name), rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call NUOPC_Realize(exportState, field=field, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    write(msg, '(A,A,A,I0,A,I0,A)') '[MetSource] Realized export "', &
      trim(g_item_name), '" on a ', g_nx, 'x', g_ny, ' uniform grid'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
  end subroutine RealizeProvided

  !> @brief Fill the export field with the constant forcing value.
  subroutine Advance(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    type(ESMF_State) :: exportState
    type(ESMF_Field) :: field
    real(esmf_kind_r8), pointer :: fptr(:,:)
    character(len=ESMF_MAXSTR) :: msg

    rc = ESMF_SUCCESS

    call NUOPC_ModelGet(gcomp, exportState=exportState, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    call ESMF_StateGet(exportState, trim(g_item_name), field, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    nullify(fptr)
    call ESMF_FieldGet(field, farrayPtr=fptr, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    ! A PET that owns no rows gets a zero-size local slab; filling it is a
    ! no-op, and the framework still timestamps the field on exit.
    if (associated(fptr)) fptr = g_value

    g_step = g_step + 1
    write(msg, '(A,F8.1,A,I0,A)') '[MetSource] Advance set export to ', &
      g_value, ' K (step ', g_step, ')'
    call ESMF_LogWrite(trim(msg), ESMF_LOGMSG_INFO)
  end subroutine Advance

  !> @brief Finalize: nothing to release (the grid stays with the component).
  subroutine Finalize(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc
    rc = ESMF_SUCCESS
    call ESMF_LogWrite('[MetSource] Finalized', ESMF_LOGMSG_INFO)
  end subroutine Finalize

  !> @brief Fill the corner-stagger coordinates of the uniform grid.
  !>
  !> Each DE's corner block spans one more point than its center block in
  !> every dimension. The corner values are derived from that DE's own
  !> center coordinates (filled by the uniform-grid constructor): the cell
  !> spacing is taken from consecutive centers, the first corner sits half a
  !> cell below the first center, and successive corners step by that
  !> spacing. Adjacent DEs share the boundary corner and compute the same
  !> value from both sides, so the filled coordinates are consistent across
  !> the decomposition.
  subroutine fill_corner_coords(grid, rc)
    type(ESMF_Grid), intent(inout) :: grid
    integer, intent(out) :: rc

    integer :: localDECount, lDE, i, j, nx_de, ny_de
    real(esmf_kind_r8), pointer :: clon(:,:), clat(:,:)
    real(esmf_kind_r8), pointer :: xlon(:,:), xlat(:,:)
    real(esmf_kind_r8) :: dlon, dlat

    rc = ESMF_SUCCESS

    call ESMF_GridGet(grid, localDECount=localDECount, rc=rc)
    if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) return  ! bail out

    do lDE = 0, localDECount - 1
      nullify(clon, clat, xlon, xlat)
      call ESMF_GridGetCoord(grid, localDE=lDE, coordDim=1, &
        staggerloc=ESMF_STAGGERLOC_CENTER, farrayPtr=clon, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
      call ESMF_GridGetCoord(grid, localDE=lDE, coordDim=2, &
        staggerloc=ESMF_STAGGERLOC_CENTER, farrayPtr=clat, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
      call ESMF_GridGetCoord(grid, localDE=lDE, coordDim=1, &
        staggerloc=ESMF_STAGGERLOC_CORNER, farrayPtr=xlon, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out
      call ESMF_GridGetCoord(grid, localDE=lDE, coordDim=2, &
        staggerloc=ESMF_STAGGERLOC_CORNER, farrayPtr=xlat, rc=rc)
      if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, &
        line=__LINE__, file=__FILE__)) return  ! bail out

      ! Cell spacing from this DE's own centers; a single-point block falls
      ! back to the global uniform spacing.
      nx_de = ubound(clon, 1) - lbound(clon, 1) + 1
      ny_de = ubound(clat, 2) - lbound(clat, 2) + 1
      if (nx_de > 1) then
        dlon = (clon(ubound(clon,1), lbound(clon,2)) - &
                clon(lbound(clon,1), lbound(clon,2))) / real(nx_de - 1, esmf_kind_r8)
      else
        dlon = (g_lon_max - g_lon_min) / real(g_nx - 1, esmf_kind_r8)
      end if
      if (ny_de > 1) then
        dlat = (clat(lbound(clat,1), ubound(clat,2)) - &
                clat(lbound(clat,1), lbound(clat,2))) / real(ny_de - 1, esmf_kind_r8)
      else
        dlat = (g_lat_max - g_lat_min) / real(g_ny - 1, esmf_kind_r8)
      end if

      do j = lbound(xlon, 2), ubound(xlon, 2)
        do i = lbound(xlon, 1), ubound(xlon, 1)
          xlon(i, j) = clon(lbound(clon,1), lbound(clon,2)) - &
            0.5_esmf_kind_r8 * dlon + &
            real(i - lbound(xlon, 1), esmf_kind_r8) * dlon
          xlat(i, j) = clat(lbound(clat,1), lbound(clat,2)) - &
            0.5_esmf_kind_r8 * dlat + &
            real(j - lbound(xlat, 2), esmf_kind_r8) * dlat
        end do
      end do
    end do
  end subroutine fill_corner_coords

end module cece_met_source_mod
