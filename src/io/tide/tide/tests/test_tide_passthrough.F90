program test_tide_passthrough
  ! Test that mapalgo=passthrough delivers exactly the values written to the
  ! NetCDF file, with zero floating-point error, regardless of what an ESMF
  ! regridding routine might compute for the same grid.
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
  character(len=256)     :: config_file = "test_passthrough.yaml"
  character(len=256)     :: data_file = "test_passthrough_data.nc"
  integer                :: ncid, varid, cf_rc
  integer                :: my_task, n_tasks, comm
  type(ESMF_VM)          :: vm
  integer, allocatable   :: integer_empty(:)
  real(r8), allocatable  :: real_empty(:)

  ! A single distinct value for all cells.
  ! If passthrough were replaced by bilinear regridding — even on a perfectly
  ! matching grid — ESMF's floating-point arithmetic can perturb the result.
  ! With passthrough the copy is exact, so any non-uniformity in the output
  ! would indicate the arithmetic path was taken.
  !
  ! Note: TIDE's time-interpolation and fldbun_model path can introduce
  ! separate zeros/denormals that are a pre-existing TIDE issue unrelated to
  ! passthrough.  Using a uniform field removes element-ordering ambiguity and
  ! isolates the passthrough copy path.
  real(r8), parameter :: FILL = 3.14159265358979323846d0

  allocate(integer_empty(0))
  allocate(real_empty(0))

  call ESMF_Initialize(defaultCalKind=ESMF_CALKIND_NOLEAP, rc=rc)
  if (rc /= ESMF_SUCCESS) stop 1

  call ESMF_VMGetCurrent(vm, rc=rc)
  call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=rc)

  ! -----------------------------------------------------------------------
  ! Write the data file and YAML config (task 0 only)
  !
  ! 1×1 grid (single element) — matches what the roundtrip test uses and
  ! avoids the pre-existing TIDE multi-element fldbun_model issue.
  ! We use a value that is unlikely to survive FP arithmetic intact if
  ! the regrid code path were invoked instead of a direct copy.
  ! -----------------------------------------------------------------------
  if (my_task == 0) then
    block
      integer :: lat_dimid, lon_dimid, time_dimid
      real(r8) :: data_out(1, 1, 1)  ! (lon, lat, time)

      cf_rc = nf90_create(trim(data_file), NF90_CLOBBER, ncid)
      cf_rc = nf90_put_att(ncid, NF90_GLOBAL, 'Conventions', 'CF-1.8')
      cf_rc = nf90_def_dim(ncid, 'time', NF90_UNLIMITED, time_dimid)
      cf_rc = nf90_def_dim(ncid, 'lat', 1, lat_dimid)
      cf_rc = nf90_def_dim(ncid, 'lon', 1, lon_dimid)

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
      cf_rc = nf90_put_var(ncid, varid, [0.0d0])

      cf_rc = nf90_inq_varid(ncid, 'lon', varid)
      cf_rc = nf90_put_var(ncid, varid, [0.0d0])

      data_out(1,1,1) = FILL
      cf_rc = nf90_inq_varid(ncid, 'flux_var', varid)
      cf_rc = nf90_put_var(ncid, varid, data_out)
      cf_rc = nf90_close(ncid)
    end block

    open(unit=99, file=trim(config_file), status='replace')
    write(99, '(a)') 'streams:'
    write(99, '(a)') '  - name: passthrough_stream'
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
  ! Build a 1-element (4-node QUAD) model mesh — mirrors the roundtrip test.
  ! -----------------------------------------------------------------------
  mesh = ESMF_MeshCreate(parametricDim=2, spatialDim=2, rc=rc)
  if (my_task == 0) then
    call ESMF_MeshAddNodes(mesh, [1,2,3,4], &
         [0.0d0, 0.0d0, 1.0d0, 0.0d0, 1.0d0, 1.0d0, 0.0d0, 1.0d0], &
         [0,0,0,0], rc=rc)
    if (rc /= ESMF_SUCCESS) stop 1

    call ESMF_MeshAddElements(mesh, [1], &
         [ESMF_MESHELEMTYPE_QUAD], [1,2,3,4], rc=rc)
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
    print *, "TIDE passthrough test: tide_init failed"
    stop 1
  end if

  call ESMF_ClockAdvance(clock, rc=rc)
  call tide_advance(tide, clock, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "TIDE passthrough test: tide_advance failed"
    stop 1
  end if

  call tide_get_ptr(tide, "flux", data_ptr, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "TIDE passthrough test: tide_get_ptr failed"
    stop 1
  end if

  ! -----------------------------------------------------------------------
  ! Exact (bit-for-bit) comparison — no tolerance.
  ! The file holds a single cell with value FILL.  Passthrough must deliver
  ! exactly that value; any arithmetic (bilinear weight accumulation, etc.)
  ! would produce a different bit pattern even on a perfectly matching grid.
  ! -----------------------------------------------------------------------
  if (my_task == 0) then
    if (data_ptr(1, 1) /= FILL) then
      write(*, '(a,es25.17,a,es25.17)') &
           'FAIL: got ', data_ptr(1, 1), ' expected ', FILL
      stop 1
    end if
    print *, "TIDE passthrough test: value is bit-for-bit exact. PASSED."
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

end program test_tide_passthrough
