!> @file cece_cap_grid.F90
!> @brief ESMF parent-grid extraction for the CECE NUOPC cap.
!>
!> The cap must be able to run on any grid a NUOPC parent provides. ESMF
!> coordinate access is Fortran-only, so this module performs the extraction
!> and assembles GLOBAL coordinate arrays across PETs (each PET holds only its
!> DE-local slice; the shared simulation core needs the full arrays). The
!> assembled coordinates are handed to the shared C-ABI facade, which
!> normalizes (longitude wrap, radian conversion), classifies the topology,
!> and validates — the same rules the config-built path runs.
!>
!> Extraction by ESMF source:
!>  - uniform ESMF_Grid with 1-D center coordinates -> rectilinear
!>  - ESMF_Grid with 2-D center coordinates (GRIDSPEC-style) -> curvilinear,
!>    flattened with x fastest to length nx*ny
!>  - ESMF_Mesh -> unstructured, global node coordinates, ny = 1
!> Anything else (non-2-D grid, Cartesian coordSys, 3-D mesh coordinates,
!> factorized coordinate layouts) fails loudly with a diagnostic naming the
!> limitation. There is no fallback to a uniform grid.
!>
!> Surplus PETs (no rows in their DE) contribute zero-count slices; the
!> gathers are collective with zero send counts, which MPI permits, so the
!> assembly never hangs. Slices are placed by global index, so DEs that
!> replicate a coordinate (a grid decomposed in both dimensions) simply write
!> identical values twice.
module cece_cap_grid_mod
  use ESMF
  implicit none
  private

  public :: cece_cap_extract_parent_grid

contains

  !> @brief Extract the GLOBAL coordinate arrays of a parent ESMF Grid or Mesh.
  !>
  !> Exactly one of @p grid / @p mesh must be provided (the cap queries
  !> gridIsPresent / meshIsPresent and passes the present object). On success
  !> nx/ny describe the grid (ny == 1 for meshes; 2-D coordinate grids keep
  !> their true nx, ny with flattened coordinates), is_rad is 1 when the ESMF
  !> object declares spherical coordinates in radians, and lon/lat carry the
  !> assembled global coordinates. On failure rc /= ESMF_SUCCESS with a named
  !> diagnostic and zero-sized lon/lat.
  subroutine cece_cap_extract_parent_grid(grid, mesh, vm, nx, ny, is_rad, lon, lat, rc)
    type(ESMF_Grid), intent(in), optional  :: grid
    type(ESMF_Mesh), intent(in), optional  :: mesh
    type(ESMF_VM),   intent(in)            :: vm
    integer,         intent(out)           :: nx
    integer,         intent(out)           :: ny
    integer,         intent(out)           :: is_rad
    real(ESMF_KIND_R8), allocatable, intent(out) :: lon(:)
    real(ESMF_KIND_R8), allocatable, intent(out) :: lat(:)
    integer,         intent(out)           :: rc

    integer :: localrc
    logical :: have_grid, have_mesh

    have_grid = present(grid)
    have_mesh = present(mesh)

    rc = ESMF_SUCCESS
    nx = 0
    ny = 0
    is_rad = 0
    allocate(lon(0), lat(0))

    if (have_grid .and. have_mesh) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[CapGrid] Both an ESMF Grid and an ESMF Mesh were provided;'// &
        ' the cap cannot choose between them.', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if
    if (.not. have_grid .and. .not. have_mesh) then
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[CapGrid] No parent ESMF Grid or Mesh supplied for grid extraction.', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if

    localrc = ESMF_SUCCESS
    if (have_grid) then
      call extract_from_grid(grid, vm, nx, ny, is_rad, lon, lat, localrc)
    else
      call extract_from_mesh(mesh, vm, nx, ny, is_rad, lon, lat, localrc)
    end if

    if (localrc /= ESMF_SUCCESS) then
      nx = 0
      ny = 0
      is_rad = 0
      allocate(lon(0), lat(0))
      rc = ESMF_FAILURE
    end if
  end subroutine cece_cap_extract_parent_grid

  !> @brief Assemble global coordinates from a 2-D ESMF Grid. The coordinate
  !> rank (1-D rectilinear vs 2-D curvilinear) is read from the center
  !> coordinate Arrays.
  subroutine extract_from_grid(grid, vm, nx, ny, is_rad, lon, lat, rc)
    type(ESMF_Grid), intent(in)  :: grid
    type(ESMF_VM),   intent(in)  :: vm
    integer,         intent(out) :: nx
    integer,         intent(out) :: ny
    integer,         intent(out) :: is_rad
    real(ESMF_KIND_R8), allocatable, intent(out) :: lon(:)
    real(ESMF_KIND_R8), allocatable, intent(out) :: lat(:)
    integer,         intent(out) :: rc

    integer :: localrc
    integer :: dimCount, gmax(2), cdc(2), i
    type(ESMF_CoordSys_Flag) :: cs
    type(ESMF_Array) :: carr
    integer :: cdc1, cdc2
    character(len=700) :: wmsg

    rc = ESMF_SUCCESS

    call ESMF_GridGet(grid, dimCount=dimCount, rc=localrc)
    if (ESMF_LogFoundError(rcToCheck=localrc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      rc = ESMF_FAILURE
      return
    end if
    call ESMF_GridGet(grid, coordSys=cs, rc=localrc)
    if (ESMF_LogFoundError(rcToCheck=localrc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      rc = ESMF_FAILURE
      return
    end if
    call ESMF_GridGet(grid, tile=1, staggerloc=ESMF_STAGGERLOC_CENTER, &
      maxIndex=gmax, rc=localrc)
    if (ESMF_LogFoundError(rcToCheck=localrc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      rc = ESMF_FAILURE
      return
    end if
    ! Coordinate-array rank (rectilinear 1-D vs curvilinear 2-D) is read
    ! from the center coordinate Arrays themselves: ESMF 8.9 does not
    ! expose coordDimCount through ESMF_GridGet, so the Array objects are
    ! fetched and queried for their rank.
    do i = 1, 2
      call ESMF_GridGetCoord(grid, coordDim=i, &
        staggerLoc=ESMF_STAGGERLOC_CENTER, array=carr, rc=localrc)
      if (localrc /= ESMF_SUCCESS) then
        write(wmsg, '(A,I0,A,I0)') '[CapGrid] ESMF_GridGetCoord(dim=', i, &
          ') center coordinate array is unavailable on the parent grid rc=', localrc
        call ESMF_LogSetError(rcToCheck=localrc, msg=trim(wmsg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return
      end if
      call ESMF_ArrayGet(carr, rank=cdc(i), rc=localrc)
      if (localrc /= ESMF_SUCCESS) then
        write(wmsg, '(A,I0,A,I0)') '[CapGrid] ESMF_ArrayGet(rank) failed for', &
          ' coordinate dim ', i, ' rc=', localrc
        call ESMF_LogSetError(rcToCheck=localrc, msg=trim(wmsg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return
      end if
      ! NOTE: carr is a reference to the grid's own internal coordinate
      ! Array (ESMF_GridGetCoord's array= variant hands back the stored
      ! pointer, it does not allocate a copy). The grid owns it and frees it
      ! with itself, so the cap must NOT destroy it here.
    end do
    cdc1 = cdc(1)
    cdc2 = cdc(2)
    if (dimCount /= 2) then
      write(wmsg, '(A,I0)') '[CapGrid] Parent ESMF Grid has dimCount=', dimCount, &
        '; CECE supports only 2-D grids (unsupported topology, no fallback).'
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if
    if (cs == ESMF_COORDSYS_SPH_RAD) then
      is_rad = 1
    else if (cs == ESMF_COORDSYS_SPH_DEG) then
      is_rad = 0
    else
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[CapGrid] Parent ESMF Grid uses a Cartesian (or unrecognized)'// &
        ' coordinate system; CECE requires lat/lon on the sphere'// &
        ' (unsupported grid type, no fallback).', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if

    nx = gmax(1)
    ny = gmax(2)

    if (cdc1 == 1 .and. cdc2 == 1) then
      call gather_rectilinear(grid, vm, nx, ny, lon, lat, localrc)
    else if (cdc1 == 2 .and. cdc2 == 2) then
      call gather_curvilinear(grid, vm, nx, ny, lon, lat, localrc)
    else
      write(wmsg, '(A,I0,A,I0)') '[CapGrid] Parent ESMF Grid coordinate arrays', &
        ' have ranks (', cdc1, ',', cdc2, '); CECE supports only 1-D', &
        ' (rectilinear) or 2-D (curvilinear) center coordinates (no fallback).'
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if
    if (localrc /= ESMF_SUCCESS) then
      rc = ESMF_FAILURE
    end if
  end subroutine extract_from_grid

  !> @brief Gather 1-D lon/lat coordinate slices into global arrays.
  !> Each PET exchanges its (global start, count) pair, then all coordinate
  !> values are assembled by placement at the global start index.
  !>
  !> Memory: ESMF_VMAllGatherV delivers the full coordinate vector to every
  !> PET (this is a replication, not a gather onto one rank), so each PET
  !> transiently holds send + receive buffers of O(nx) and O(ny) doubles —
  !> the same layout as the global arrays the facade needs anyway. Even at
  !> extreme resolutions this is tiny next to the per-PET field storage
  !> (nx*ny*nz), because 1-D coordinates scale with the grid's linear
  !> dimensions, not its area.
  subroutine gather_rectilinear(grid, vm, nx, ny, lon, lat, rc)
    type(ESMF_Grid), intent(in)  :: grid
    type(ESMF_VM),   intent(in)  :: vm
    integer,         intent(in)  :: nx
    integer,         intent(in)  :: ny
    real(ESMF_KIND_R8), allocatable, intent(out) :: lon(:)
    real(ESMF_KIND_R8), allocatable, intent(out) :: lat(:)
    integer,         intent(out) :: rc

    integer :: localrc, petCount, p, i
    integer :: lbnd(2), ubnd(2)
    integer :: local_count
    real(ESMF_KIND_R8), pointer :: ptr(:)
    integer, allocatable :: meta(:)     ! 2 per PET: start, count
    integer, allocatable :: counts(:), offsets(:)
    real(ESMF_KIND_R8), allocatable :: sendbuf(:), recvbuf(:)
    integer :: start_p, count_p
    character(len=700) :: wmsg

    rc = ESMF_SUCCESS
    call ESMF_VMGet(vm, petCount=petCount, rc=localrc)
    if (ESMF_LogFoundError(rcToCheck=localrc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      rc = ESMF_FAILURE
      return
    end if

    allocate(lon(nx), lat(ny))
    lon = 0.0d0
    lat = 0.0d0
    allocate(meta(0:2*petCount-1), counts(0:petCount-1), offsets(0:petCount-1))

    do i = 1, 2
      nullify(ptr)
      call ESMF_GridGetCoord(grid, coordDim=i, localDE=0, &
        staggerLoc=ESMF_STAGGERLOC_CENTER, &
        computationalLBound=lbnd, computationalUBound=ubnd, &
        farrayPtr=ptr, rc=localrc)
      if (localrc /= ESMF_SUCCESS) then
        write(wmsg, '(A,I0,A,I0)') '[CapGrid] ESMF_GridGetCoord(dim=', i, &
          ') failed for a rectilinear parent grid rc=', localrc
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return
      end if
      local_count = ubnd(1) - lbnd(1) + 1
      if (local_count < 0) local_count = 0

      call exchange_int_meta(vm, (/lbnd(1), local_count/), meta, localrc)
      if (localrc /= ESMF_SUCCESS) then
        rc = ESMF_FAILURE
        return
      end if

      allocate(sendbuf(0:max(local_count, 0)-1))
      if (local_count > 0) sendbuf(0:local_count-1) = ptr(lbnd(1):ubnd(1))

      do p = 0, petCount-1
        counts(p) = meta(2*p+1)
      end do
      offsets(0) = 0
      do p = 1, petCount-1
        offsets(p) = offsets(p-1) + counts(p-1)
      end do

      allocate(recvbuf(0:max(sum(counts), 0)-1))
      call ESMF_VMAllGatherV(vm, sendData=sendbuf, sendCount=local_count, &
        recvData=recvbuf, recvCounts=counts, recvOffsets=offsets, rc=localrc)
      if (localrc /= ESMF_SUCCESS) then
        deallocate(sendbuf, recvbuf)
        rc = ESMF_FAILURE
        return
      end if
      do p = 0, petCount-1
        start_p = meta(2*p+0)
        count_p = meta(2*p+1)
        if (count_p > 0) then
          if (i == 1) then
            lon(start_p:start_p+count_p-1) = &
              recvbuf(offsets(p):offsets(p)+count_p-1)
          else
            lat(start_p:start_p+count_p-1) = &
              recvbuf(offsets(p):offsets(p)+count_p-1)
          end if
        end if
      end do
      deallocate(sendbuf, recvbuf)
    end do
  end subroutine gather_rectilinear

  !> @brief Gather 2-D lon/lat coordinate blocks into flattened global arrays
  !> (x fastest), matching the C++ curvilinear convention. Each PET exchanges
  !> (i0, j0, ix, jy) — the global origin and shape of its computational
  !> block — so placement is exact regardless of DE decomposition.
  !>
  !> Memory: like the rectilinear path, ESMF_VMAllGatherV replicates the full
  !> coordinate set onto every PET (not onto a single rank), here O(nx*ny)
  !> doubles per coordinate. That is the size the shared facade's GridSpec
  !> must hold globally anyway; the transient send/receive buffers are the
  !> only extra cost, and they are released when the gather returns.
  subroutine gather_curvilinear(grid, vm, nx, ny, lon, lat, rc)
    type(ESMF_Grid), intent(in)  :: grid
    type(ESMF_VM),   intent(in)  :: vm
    integer,         intent(in)  :: nx
    integer,         intent(in)  :: ny
    real(ESMF_KIND_R8), allocatable, intent(out) :: lon(:)
    real(ESMF_KIND_R8), allocatable, intent(out) :: lat(:)
    integer,         intent(out) :: rc

    integer :: localrc, petCount, p, i, gi, gj
    integer :: lbnd(2), ubnd(2)
    integer :: local_ix, local_jy, local_count
    real(ESMF_KIND_R8), pointer :: ptr(:,:)
    integer, allocatable :: meta(:)     ! 4 per PET: i0, j0, ix, jy
    integer, allocatable :: counts(:), offsets(:)
    real(ESMF_KIND_R8), allocatable :: sendbuf(:), recvbuf(:)
    integer :: i0, j0, ix, jy, src
    character(len=700) :: wmsg

    rc = ESMF_SUCCESS
    call ESMF_VMGet(vm, petCount=petCount, rc=localrc)
    if (ESMF_LogFoundError(rcToCheck=localrc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      rc = ESMF_FAILURE
      return
    end if

    allocate(lon(nx*ny), lat(nx*ny))
    lon = 0.0d0
    lat = 0.0d0
    allocate(meta(0:4*petCount-1), counts(0:petCount-1), offsets(0:petCount-1))

    do i = 1, 2
      nullify(ptr)
      call ESMF_GridGetCoord(grid, coordDim=i, localDE=0, &
        staggerLoc=ESMF_STAGGERLOC_CENTER, &
        computationalLBound=lbnd, computationalUBound=ubnd, &
        farrayPtr=ptr, rc=localrc)
      if (localrc /= ESMF_SUCCESS) then
        write(wmsg, '(A,I0,A,I0)') '[CapGrid] ESMF_GridGetCoord(dim=', i, &
          ') failed for a curvilinear parent grid rc=', localrc
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return
      end if
      local_ix = ubnd(1) - lbnd(1) + 1
      local_jy = ubnd(2) - lbnd(2) + 1
      if (local_ix < 0 .or. local_jy < 0) then
        local_ix = 0
        local_jy = 0
      end if
      local_count = local_ix * local_jy

      call exchange_int_meta(vm, (/lbnd(1), lbnd(2), local_ix, local_jy/), meta, localrc)
      if (localrc /= ESMF_SUCCESS) then
        rc = ESMF_FAILURE
        return
      end if

      allocate(sendbuf(0:max(local_count, 0)-1))
      if (local_count > 0) then
        ! Fortran pointer memory order is x-fastest: matches the flattened
        ! curvilinear convention expected by the shared C++ grid code.
        sendbuf(0:local_count-1) = &
          reshape(ptr(lbnd(1):ubnd(1), lbnd(2):ubnd(2)), [local_count])
      end if

      do p = 0, petCount-1
        counts(p) = meta(4*p+2) * meta(4*p+3)
      end do
      offsets(0) = 0
      do p = 1, petCount-1
        offsets(p) = offsets(p-1) + counts(p-1)
      end do

      allocate(recvbuf(0:max(sum(counts), 0)-1))
      call ESMF_VMAllGatherV(vm, sendData=sendbuf, sendCount=local_count, &
        recvData=recvbuf, recvCounts=counts, recvOffsets=offsets, rc=localrc)
      if (localrc /= ESMF_SUCCESS) then
        deallocate(sendbuf, recvbuf)
        rc = ESMF_FAILURE
        return
      end if

      do p = 0, petCount-1
        i0 = meta(4*p+0)
        j0 = meta(4*p+1)
        ix = meta(4*p+2)
        jy = meta(4*p+3)
        do gj = 0, jy-1
          do gi = 0, ix-1
            src = offsets(p) + gj*ix + gi
            ! flattened x-fastest global index (1-based output)
            if (i == 1) then
              lon(((j0-1)+gj)*nx + ((i0-1)+gi) + 1) = recvbuf(src)
            else
              lat(((j0-1)+gj)*nx + ((i0-1)+gi) + 1) = recvbuf(src)
            end if
          end do
        end do
      end do
      deallocate(sendbuf, recvbuf)
    end do
  end subroutine gather_curvilinear

  !> @brief Assemble global node coordinates from an ESMF Mesh (unstructured).
  !> ESMF_MeshGet returns the full node set replicated on every PET, so no
  !> cross-PET assembly is needed for meshes.
  subroutine extract_from_mesh(mesh, vm, nx, ny, is_rad, lon, lat, rc)
    type(ESMF_Mesh), intent(in)  :: mesh
    type(ESMF_VM),   intent(in)  :: vm
    integer,         intent(out) :: nx
    integer,         intent(out) :: ny
    integer,         intent(out) :: is_rad
    real(ESMF_KIND_R8), allocatable, intent(out) :: lon(:)
    real(ESMF_KIND_R8), allocatable, intent(out) :: lat(:)
    integer,         intent(out) :: rc

    integer :: localrc, sdim, nodeCount, k
    type(ESMF_CoordSys_Flag) :: cs
    integer, allocatable :: ids(:)
    real(ESMF_KIND_R8), allocatable :: coords(:)
    character(len=700) :: wmsg

    rc = ESMF_SUCCESS

    call ESMF_MeshGet(mesh, spatialDim=sdim, coordSys=cs, nodeCount=nodeCount, &
      rc=localrc)
    if (ESMF_LogFoundError(rcToCheck=localrc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      rc = ESMF_FAILURE
      return
    end if
    if (sdim /= 2) then
      write(wmsg, '(A,I0)') '[CapGrid] Parent ESMF Mesh has spatialDim=', sdim, &
        '; CECE supports only 2-D (lat/lon) meshes (unsupported mesh type, no fallback).'
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if
    if (cs == ESMF_COORDSYS_SPH_RAD) then
      is_rad = 1
    else if (cs == ESMF_COORDSYS_SPH_DEG) then
      is_rad = 0
    else
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, &
        msg='[CapGrid] Parent ESMF Mesh uses a Cartesian (or unrecognized)'// &
        ' coordinate system; CECE requires lat/lon on the sphere'// &
        ' (unsupported mesh type, no fallback).', &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if

    nx = nodeCount
    ny = 1
    allocate(lon(max(nx, 0)), lat(max(nx, 0)))
    if (nx <= 0) return
    lon = 0.0d0
    lat = 0.0d0

    allocate(ids(nodeCount), coords(2*nodeCount))
    call ESMF_MeshGet(mesh, nodeIds=ids, nodeCoords=coords, rc=localrc)
    if (ESMF_LogFoundError(rcToCheck=localrc, msg=ESMF_LOGERR_PASSTHRU, &
      line=__LINE__, file=__FILE__)) then
      rc = ESMF_FAILURE
      return
    end if
    do k = 1, nodeCount
      if (ids(k) < 1 .or. ids(k) > nx) then
        write(wmsg, '(A,I0)') '[CapGrid] Mesh node id out of range: ', ids(k)
        call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
          line=__LINE__, file=__FILE__, rcToReturn=rc)
        return
      end if
      lon(ids(k)) = coords(2*k-1)
      lat(ids(k)) = coords(2*k)
    end do
  end subroutine extract_from_mesh

  !> @brief AllGather a fixed-size integer metadata vector on every PET.
  !> @p recv_all uses the ESMF VM-collective 0-based layout: PET p's values
  !> land in recv_all(n*p : n*(p+1)-1).
  subroutine exchange_int_meta(vm, send_vals, recv_all, rc)
    type(ESMF_VM), intent(in)  :: vm
    integer, intent(in)        :: send_vals(:)
    integer, intent(out)       :: recv_all(0:)
    integer, intent(out)       :: rc

    integer :: localrc, petCount, n
    character(len=700) :: wmsg

    call ESMF_VMGet(vm, petCount=petCount, rc=localrc)
    if (localrc /= ESMF_SUCCESS) then
      rc = localrc
      return
    end if
    n = size(send_vals)
    if (size(recv_all) < n*petCount) then
      wmsg = '[CapGrid] internal gather buffer too small'
      call ESMF_LogSetError(rcToCheck=ESMF_FAILURE, msg=trim(wmsg), &
        line=__LINE__, file=__FILE__, rcToReturn=rc)
      return
    end if
    call ESMF_VMAllGather(vm, sendData=send_vals, recvData=recv_all, count=n, &
      rc=localrc)
    if (localrc /= ESMF_SUCCESS) then
      rc = localrc
      return
    end if
    rc = ESMF_SUCCESS
  end subroutine exchange_int_meta

end module cece_cap_grid_mod
