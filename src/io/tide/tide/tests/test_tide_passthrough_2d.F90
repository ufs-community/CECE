program test_tide_passthrough_2d
  ! Test that mapalgo=passthrough correctly delivers 2-D lat/lon data
  ! (the primary real-world use case: data already on the model grid).
  !
  ! Setup: 2×2 lat/lon NetCDF file (4 cells with distinct values) read
  ! onto a 4-element model mesh.  After passthrough the mesh elements must
  ! hold exactly the values written to the file, with no floating-point
  ! modification.
  !
  ! Why passthrough guarantees bit-exactness:
  !   - The passthrough path copies dataptr1d(:) directly to the model
  !     field pointer without calling ESMF_FieldRegrid.
  !   - With a single-timestep file and taxmode=cycle the time-interpolation
  !     factors are (flb=1, fub=0) or (flb=fub=0.5). Both reduce to an
  !     exact identity when LB==UB for non-overflowing IEEE-754 values.
  !
  ! Index convention: tide_get_ptr reshapes the 1-D mesh pointer to
  ! (nelems, 1), so the check uses data_ptr(elem_index, 1).
  !
  ! Stream seqIndex (from ESMF_DistGridCreate minIndex=(/1,1/), maxIndex=(/2,2/)):
  !   (lon_i=1, lat_j=1) → seq 1 → elem 1 → data_out(1,1,1)
  !   (lon_i=2, lat_j=1) → seq 2 → elem 2 → data_out(2,1,1)
  !   (lon_i=1, lat_j=2) → seq 3 → elem 3 → data_out(1,2,1)
  !   (lon_i=2, lat_j=2) → seq 4 → elem 4 → data_out(2,2,1)
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
  character(len=256)     :: config_file = "test_passthrough_2d.yaml"
  character(len=256)     :: data_file = "test_passthrough_2d_data.nc"
  integer                :: ncid, varid, cf_rc
  integer                :: my_task, n_tasks, comm
  type(ESMF_VM)          :: vm
  integer, allocatable   :: integer_empty(:)
  real(r8), allocatable  :: real_empty(:)
  integer                :: n

  ! Distinct irrational-looking values for each of the 4 grid cells.
  ! Laid out as (lon_index, lat_index) to match the NetCDF write below.
  !
  ! data_out(1,1) = lon1/lat1 = expected(1)   seq index 1 → elem 1
  ! data_out(2,1) = lon2/lat1 = expected(2)   seq index 2 → elem 2
  ! data_out(1,2) = lon1/lat2 = expected(3)   seq index 3 → elem 3
  ! data_out(2,2) = lon2/lat2 = expected(4)   seq index 4 → elem 4
  real(r8), parameter :: expected(4) = [ &
       3.14159265358979d0, &   ! π
       2.71828182845905d0, &   ! e
       1.41421356237310d0, &   ! √2
       1.61803398874989d0  ]   ! φ

  allocate(integer_empty(0))
  allocate(real_empty(0))

  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_NOLEAP, rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call ESMF_VMGetCurrent(vm, rc=rc)
  call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=rc)

  ! -----------------------------------------------------------------------
  ! Task 0: write 2×2 lat/lon NetCDF and YAML config
  ! -----------------------------------------------------------------------
  if (my_task == 0) then
    block
      integer :: lat_dimid, lon_dimid, time_dimid
      real(r8) :: data_out(2, 2, 1)   ! (lon, lat, time)

      cf_rc = nf90_create(trim(data_file), NF90_CLOBBER, ncid)
      cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
      cf_rc = nf90_def_dim(ncid, 'time', NF90_UNLIMITED, time_dimid)
      cf_rc = nf90_def_dim(ncid, 'lat', 2, lat_dimid)
      cf_rc = nf90_def_dim(ncid, 'lon', 2, lon_dimid)

      cf_rc = nf90_def_var(ncid, 'time', NF90_DOUBLE, [time_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'days since 2000-01-01 00:00:00')
      cf_rc = nf90_put_att(ncid, varid, 'calendar', 'noleap')

      cf_rc = nf90_def_var(ncid, 'lat', NF90_DOUBLE, [lat_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'degrees_north')

      cf_rc = nf90_def_var(ncid, 'lon', NF90_DOUBLE, [lon_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'degrees_east')

      cf_rc = nf90_def_var(ncid, 'flux_var', NF90_DOUBLE, &
           [lon_dimid, lat_dimid, time_dimid], varid)
      cf_rc = nf90_put_att(ncid, varid, 'units', 'kg m-2 s-1')
      cf_rc = nf90_put_att(ncid, varid, 'standard_name', 'flux_std')
      cf_rc = nf90_put_att(ncid, varid, 'coordinates', 'lon lat time')
      cf_rc = nf90_enddef(ncid)

      cf_rc = nf90_inq_varid(ncid, 'time', varid)
      cf_rc = nf90_put_var(ncid, varid, [0.0d0])

      cf_rc = nf90_inq_varid(ncid, 'lat', varid)
      cf_rc = nf90_put_var(ncid, varid, [0.0d0, 1.0d0])

      cf_rc = nf90_inq_varid(ncid, 'lon', varid)
      cf_rc = nf90_put_var(ncid, varid, [0.0d0, 1.0d0])

      ! Fortran column-major NetCDF write: data_out(lon, lat, time)
      data_out(1,1,1) = expected(1)   ! seq 1
      data_out(2,1,1) = expected(2)   ! seq 2
      data_out(1,2,1) = expected(3)   ! seq 3
      data_out(2,2,1) = expected(4)   ! seq 4

      cf_rc = nf90_inq_varid(ncid, 'flux_var', varid)
      cf_rc = nf90_put_var(ncid, varid, data_out)
      cf_rc = nf90_close(ncid)
    end block

    open(unit=99, file=trim(config_file), status='replace')
    write(99, '(a)') 'streams:'
    write(99, '(a)') '  - name: passthrough_2d_stream'
    write(99, '(a)') '    tax_mode: "cycle"'
    write(99, '(a)') '    time_interp: "linear"'
    write(99, '(a)') '    map_algo: "passthrough"'
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
  ! Build a 4-element (2×2) model mesh with 9 nodes.
  !
  ! Node grid (col=lon, row=lat):
  !   7:(0,2)  8:(1,2)  9:(2,2)
  !   4:(0,1)  5:(1,1)  6:(2,1)
  !   1:(0,0)  2:(1,0)  3:(2,0)
  !
  ! Element IDs match the ESMF sequential grid index order
  ! (lon-varying fastest = Fortran column-major):
  !   elem 1: seq 1, nodes 1,2,5,4 → lon_i=1, lat_j=1
  !   elem 2: seq 2, nodes 2,3,6,5 → lon_i=2, lat_j=1
  !   elem 3: seq 3, nodes 4,5,8,7 → lon_i=1, lat_j=2
  !   elem 4: seq 4, nodes 5,6,9,8 → lon_i=2, lat_j=2
  ! -----------------------------------------------------------------------
  mesh = ESMF_MeshCreate(parametricDim=2, spatialDim=2, rc=rc)
  if (my_task == 0) then
    call ESMF_MeshAddNodes(mesh, &
         nodeIds=[1,2,3,4,5,6,7,8,9], &
         nodeCoords=[ &
           0.0d0, 0.0d0,  &   ! node 1
           1.0d0, 0.0d0,  &   ! node 2
           2.0d0, 0.0d0,  &   ! node 3
           0.0d0, 1.0d0,  &   ! node 4
           1.0d0, 1.0d0,  &   ! node 5
           2.0d0, 1.0d0,  &   ! node 6
           0.0d0, 2.0d0,  &   ! node 7
           1.0d0, 2.0d0,  &   ! node 8
           2.0d0, 2.0d0   &   ! node 9
         ], &
         nodeMask=[0,0,0,0,0,0,0,0,0], rc=rc)
    if (rc /= ESMF_SUCCESS) stop 1

    call ESMF_MeshAddElements(mesh, &
         elementIds=[1,2,3,4], &
         elementTypes=[ESMF_MESHELEMTYPE_QUAD, ESMF_MESHELEMTYPE_QUAD, &
                       ESMF_MESHELEMTYPE_QUAD, ESMF_MESHELEMTYPE_QUAD], &
         elementConn=[1,2,5,4, 2,3,6,5, 4,5,8,7, 5,6,9,8], rc=rc)
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
    print *, "TIDE passthrough_2d test: tide_init failed"
    stop 1
  end if

  call ESMF_ClockAdvance(clock, rc=rc)
  call tide_advance(tide, clock, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "TIDE passthrough_2d test: tide_advance failed"
    stop 1
  end if

  call tide_get_ptr(tide, "flux", data_ptr, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "TIDE passthrough_2d test: tide_get_ptr failed"
    stop 1
  end if

  ! -----------------------------------------------------------------------
  ! Bit-exact comparison.
  !
  ! tide_get_ptr reshapes the 1-D mesh pointer (nelems,) to (nelems, 1),
  ! so the correct index is data_ptr(elem, 1) — NOT data_ptr(1, elem).
  !
  ! Time interpolation: with a single time step and taxmode=cycle, LB==UB,
  ! so factors are (1,0) or (0.5,0.5). Both reduce to the identity for
  ! IEEE-754 non-overflowing values, preserving bit-exactness.
  ! -----------------------------------------------------------------------
  if (my_task == 0) then
    do n = 1, 4
      if (data_ptr(n, 1) /= expected(n)) then
        write(*, '(a,i0,a,es25.17,a,es25.17)') &
             'FAIL: element ', n, &
             ' got ', data_ptr(n, 1), &
             ' expected ', expected(n)
        stop 1
      end if
    end do
    print *, "TIDE passthrough_2d test: all 4 values bit-for-bit exact. PASSED."
  end if

  call tide_finalize(tide, rc)

  ! Clean up temp files
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

end program test_tide_passthrough_2d
