program test_tide_consd
  ! Diagnostic test: conservative-with-destination-normalisation (consd)
  ! regridding from a 2×2 lat/lon source grid to a 4-element model mesh
  ! whose element extents coincide exactly with the source cells.
  !
  ! When source and destination cells are geometrically identical, consd
  ! regridding should reproduce the source values to machine precision.
  ! If ESMF gives zeros here, the regrid weights themselves are wrong
  ! (coordinate-system mismatch or all-masked source/dest).
  !
  ! Grid layout (same as test_tide_bilinear):
  !   (lon_i=1, lat_j=1) → elem 1 → expected(1)
  !   (lon_i=2, lat_j=1) → elem 2 → expected(2)
  !   (lon_i=1, lat_j=2) → elem 3 → expected(3)
  !   (lon_i=2, lat_j=2) → elem 4 → expected(4)
  use tide_mod
  use ESMF
  use pio
  use netcdf
  use shr_kind_mod, only : r8 => shr_kind_r8
  implicit none

  type(tide_type)        :: tide
  type(ESMF_Mesh)        :: mesh
  type(ESMF_Clock)       :: clock
  type(ESMF_Time)        :: startTime, stopTime
  type(ESMF_TimeInterval):: timeStep
  integer                :: rc
  real(r8), pointer      :: data_ptr(:,:)
  character(len=256)     :: config_file = "test_consd.yaml"
  character(len=256)     :: data_file = "test_consd_data.nc"
  integer                :: ncid, varid, cf_rc
  integer                :: my_task, n_tasks, comm
  type(ESMF_VM)          :: vm
  integer, allocatable   :: integer_empty(:)
  real(r8), allocatable  :: real_empty(:)
  integer                :: n
  logical                :: all_passed

  ! Same distinct values as the bilinear test.
  real(r8), parameter :: expected(4) = [ &
       3.14159265358979d0, &   ! π  → elem 1 (lon1/lat1)
       2.71828182845905d0, &   ! e  → elem 2 (lon2/lat1)
       1.41421356237310d0, &   ! √2 → elem 3 (lon1/lat2)
       1.61803398874989d0  ]   ! φ  → elem 4 (lon2/lat2)

  ! 1% relative tolerance — consd on same grid should be exact,
  ! but we allow a loose tolerance so the test still reports values.
  real(r8), parameter :: TOL = 0.01d0

  allocate(integer_empty(0))
  allocate(real_empty(0))

  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_NOLEAP, rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call ESMF_VMGetCurrent(vm, rc=rc)
  call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=rc)

  ! -----------------------------------------------------------------------
  ! Write 2×2 NetCDF (lon=0,1, lat=0,1) and YAML config on task 0.
  ! -----------------------------------------------------------------------
  if (my_task == 0) then
    block
      integer :: lat_dimid, lon_dimid, time_dimid, bnds_dimid
      real(r8) :: data_out(2, 2, 1)   ! (lon, lat, time)

      cf_rc = nf90_create(trim(data_file), NF90_CLOBBER, ncid)
      cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
      cf_rc = nf90_def_dim(ncid, 'time', NF90_UNLIMITED, time_dimid)
      cf_rc = nf90_def_dim(ncid, 'lat', 2, lat_dimid)
      cf_rc = nf90_def_dim(ncid, 'lon', 2, lon_dimid)
      cf_rc = nf90_def_dim(ncid, 'bnds', 2, bnds_dimid)  ! for bounds variables

      cf_rc = nf90_def_var(ncid, 'time', NF90_DOUBLE, [time_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'days since 2000-01-01 00:00:00')
      cf_rc = nf90_put_att(ncid, varid, 'calendar', 'noleap')

      cf_rc = nf90_def_var(ncid, 'lat', NF90_DOUBLE, [lat_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'degrees_north')
      cf_rc = nf90_put_att(ncid, varid, 'bounds', 'lat_bnds')

      cf_rc = nf90_def_var(ncid, 'lat_bnds', NF90_DOUBLE, [bnds_dimid, lat_dimid], varid)

      cf_rc = nf90_def_var(ncid, 'lon', NF90_DOUBLE, [lon_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'degrees_east')
      cf_rc = nf90_put_att(ncid, varid, 'bounds', 'lon_bnds')

      cf_rc = nf90_def_var(ncid, 'lon_bnds', NF90_DOUBLE, [bnds_dimid, lon_dimid], varid)

      cf_rc = nf90_def_var(ncid, 'flux_var', NF90_DOUBLE, &
           [lon_dimid, lat_dimid, time_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'kg m-2 s-1')
      cf_rc = nf90_put_att(ncid, varid, 'standard_name', 'flux_std')
      cf_rc = nf90_put_att(ncid, varid, 'coordinates', 'lon lat time')
      cf_rc = nf90_enddef(ncid)

      cf_rc = nf90_inq_varid(ncid, 'time', varid)
      cf_rc = nf90_put_var(ncid, varid, [0.0d0])

      cf_rc = nf90_inq_varid(ncid, 'lat', varid)
      cf_rc = nf90_put_var(ncid, varid, [0.25d0, 0.75d0])  ! cell centres at 0.25, 0.75

      ! lat_bnds(bnds=2, lat=2): lower and upper edge of each cell
      cf_rc = nf90_inq_varid(ncid, 'lat_bnds', varid)
      cf_rc = nf90_put_var(ncid, varid, reshape([0.0d0, 0.5d0, 0.5d0, 1.0d0], [2,2]))

      cf_rc = nf90_inq_varid(ncid, 'lon', varid)
      cf_rc = nf90_put_var(ncid, varid, [0.25d0, 0.75d0])  ! cell centres at 0.25, 0.75

      ! lon_bnds(bnds=2, lon=2): lower and upper edge of each cell
      cf_rc = nf90_inq_varid(ncid, 'lon_bnds', varid)
      cf_rc = nf90_put_var(ncid, varid, reshape([0.0d0, 0.5d0, 0.5d0, 1.0d0], [2,2]))

      data_out(1,1,1) = expected(1)
      data_out(2,1,1) = expected(2)
      data_out(1,2,1) = expected(3)
      data_out(2,2,1) = expected(4)

      cf_rc = nf90_inq_varid(ncid, 'flux_var', varid)
      cf_rc = nf90_put_var(ncid, varid, data_out)
      cf_rc = nf90_close(ncid)
    end block

    open(unit=99, file=trim(config_file), status='replace')
    write(99, '(a)') 'streams:'
    write(99, '(a)') '  - name: consd_test_stream'
    write(99, '(a)') '    tax_mode: "cycle"'
    write(99, '(a)') '    time_interp: "linear"'
    write(99, '(a)') '    map_algo: "consd"'
    write(99, '(a)') '    year_first: 2000'
    write(99, '(a)') '    year_last: 2000'
    write(99, '(a)') '    year_align: 2000'
    write(99, '(a)') '    cf_detection: "auto"'
    write(99, '(a)') '    input_files:'
    write(99, '(a,a,a)') '      - "', trim(data_file), '"'
    write(99, '(a)') '    field_maps:'
    write(99, '(a)') '      - { file_var: "flux_var", model_var: "flux" }'
    close(99)
  end if

  call ESMF_VMBroadcast(vm, config_file, 256, 0, rc=rc)

  ! -----------------------------------------------------------------------
  ! Build a 4-element (2×2) model mesh whose element extents coincide with
  ! the source cells (lon=0–0.5/0.5–1, lat=0–0.5/0.5–1).
  !
  ! Node grid:
  !   7:(0,1)   8:(0.5,1)  9:(1,1)
  !   4:(0,0.5) 5:(0.5,0.5) 6:(1,0.5)
  !   1:(0,0)   2:(0.5,0)  3:(1,0)
  !
  ! Element centres (average of 4 nodes = source cell centres):
  !   elem 1: nodes 1,2,5,4 → centre (0.25, 0.25)  ← matches expected(1)
  !   elem 2: nodes 2,3,6,5 → centre (0.75, 0.25)  ← matches expected(2)
  !   elem 3: nodes 4,5,8,7 → centre (0.25, 0.75)  ← matches expected(3)
  !   elem 4: nodes 5,6,9,8 → centre (0.75, 0.75)  ← matches expected(4)
  !
  ! For conservative regridding the mesh areas must match the source cell
  ! areas, which they do here because the grids are identical.
  ! -----------------------------------------------------------------------
  mesh = ESMF_MeshCreate(parametricDim=2, spatialDim=2, rc=rc)
  if (my_task == 0) then
    call ESMF_MeshAddNodes(mesh, &
         nodeIds=[1,2,3,4,5,6,7,8,9], &
         nodeCoords=[ &
           0.00d0, 0.00d0,  &  ! node 1
           0.50d0, 0.00d0,  &  ! node 2
           1.00d0, 0.00d0,  &  ! node 3
           0.00d0, 0.50d0,  &  ! node 4
           0.50d0, 0.50d0,  &  ! node 5
           1.00d0, 0.50d0,  &  ! node 6
           0.00d0, 1.00d0,  &  ! node 7
           0.50d0, 1.00d0,  &  ! node 8
           1.00d0, 1.00d0   &  ! node 9
         ], &
         nodeMask=[0,0,0,0,0,0,0,0,0], rc=rc)
    if (rc /= ESMF_SUCCESS) stop 1

    call ESMF_MeshAddElements(mesh, &
         elementIds=[1,2,3,4], &
         elementTypes=[ESMF_MESHELEMTYPE_QUAD, ESMF_MESHELEMTYPE_QUAD, &
                       ESMF_MESHELEMTYPE_QUAD, ESMF_MESHELEMTYPE_QUAD], &
         elementConn=[1,2,5,4, 2,3,6,5, 4,5,8,7, 5,6,9,8], &
         elementCoords=[0.25d0,0.25d0, 0.75d0,0.25d0, 0.25d0,0.75d0, 0.75d0,0.75d0], rc=rc)
    if (rc /= ESMF_SUCCESS) stop 1
  else
    call ESMF_MeshAddNodes(mesh, integer_empty, real_empty, integer_empty, rc=rc)
    call ESMF_MeshAddElements(mesh, integer_empty, integer_empty, integer_empty, rc=rc)
  end if

  ! -----------------------------------------------------------------------
  ! TIDE init / advance / check
  ! -----------------------------------------------------------------------
  call ESMF_TimeSet(startTime, yy=2000, mm=1, dd=1, s=0, rc=rc)
  call ESMF_TimeSet(stopTime,  yy=2000, mm=1, dd=2, s=0, rc=rc)
  call ESMF_TimeIntervalSet(timeStep, d=1, rc=rc)
  clock = ESMF_ClockCreate(timeStep, startTime, stopTime=stopTime, rc=rc)

  call tide_init(tide, config_yaml=config_file, model_mesh=mesh, clock=clock, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "TIDE consd test: tide_init failed"
    stop 1
  end if

  call ESMF_ClockAdvance(clock, rc=rc)
  call tide_advance(tide, clock, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "TIDE consd test: tide_advance failed"
    stop 1
  end if

  call tide_get_ptr(tide, "flux", data_ptr, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "TIDE consd test: tide_get_ptr failed"
    stop 1
  end if

  ! -----------------------------------------------------------------------
  ! Check results.  Print values regardless so they can be inspected.
  ! -----------------------------------------------------------------------
  if (my_task == 0) then
    all_passed = .true.
    do n = 1, 4
      write(*, '(a,i0,a,es25.17,a,es25.17)') &
           'elem ', n, ': got ', data_ptr(n, 1), '  expected ', expected(n)
      if (abs(data_ptr(n, 1) - expected(n)) > TOL * abs(expected(n))) then
        write(*, '(a,i0,a,f10.4,a)') &
             '  FAIL: relative error = ', &
             int(abs(data_ptr(n, 1) - expected(n)) / abs(expected(n)) * 100), &
             '% > tolerance ', TOL * 100.0d0, '%'
        all_passed = .false.
      else
        print *, '  OK'
      end if
    end do

    if (all_passed) then
      print *, "TIDE consd test: all 4 values within tolerance. PASSED."
    else
      print *, "TIDE consd test: one or more values out of tolerance. FAILED."
      stop 1
    end if
  end if

  call tide_finalize(tide, rc)

  if (my_task == 0) then
    block
      integer :: ios
      open(unit=99, file=trim(data_file),   status='old', iostat=ios)
      if (ios == 0) close(99, status='delete')
      open(unit=99, file=trim(config_file), status='old', iostat=ios)
      if (ios == 0) close(99, status='delete')
    end block
  end if

  call ESMF_MeshDestroy(mesh, rc=rc)
  call ESMF_ClockDestroy(clock, rc=rc)
  call ESMF_Finalize(rc=rc)

end program test_tide_consd
