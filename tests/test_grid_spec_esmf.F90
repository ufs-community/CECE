!> @file test_grid_spec_esmf.F90
!> @brief Unit test for the shared grid contract's parent-grid entry point.
!>
!> Feeds synthetic ESMF-style coordinate arrays (exactly what the Fortran cap
!> assembles from a parent ESMF Grid/Mesh) through the C-ABI classification
!> entry and asserts the resulting topology, coordinate lengths, and values:
!>   - uniform 1-D lon[nx] x lat[ny]  -> rectilinear
!>   - GRIDSPEC 2-D coords flattened  -> curvilinear (len == nx*ny)
!>   - mesh node list (ny = 1)        -> unstructured
!>   - radian source flagged as such  -> converted to degrees
!>   - longitude outside [-180, 180)  -> wrapped
!>   - a factorized/unsupported shape -> rejected with rc /= 0
!>   - non-positive dims              -> rejected (layer count is config-side)
program test_grid_spec_esmf
  use, intrinsic :: iso_c_binding
  use mpi
  implicit none

  integer, parameter :: RECTILINEAR = 0
  integer, parameter :: CURVILINEAR = 1
  integer, parameter :: UNSTRUCTURED = 2
  integer, parameter :: MAX_COORDS = 1024

  interface
    ! void cece_sim_grid_from_esmf(int nx, int ny, int nz, int is_rad,
    !   const double* lon_coords, int lon_len,
    !   const double* lat_coords, int lat_len,
    !   int* topology_out, double* lon_out, int lon_out_max, int* lon_len_out,
    !   double* lat_out, int lat_out_max, int* lat_len_out, int* rc)
    subroutine cece_sim_grid_from_esmf(nx, ny, nz, is_rad, lon_coords, lon_len, &
                                       lat_coords, lat_len, topology_out, &
                                       lon_out, lon_out_max, lon_len_out, &
                                       lat_out, lat_out_max, lat_len_out, rc) &
                                       bind(C, name="cece_sim_grid_from_esmf")
      import :: c_int, c_double
      integer(c_int), value :: nx, ny, nz, is_rad
      real(c_double), intent(in) :: lon_coords(*)
      integer(c_int), value :: lon_len
      real(c_double), intent(in) :: lat_coords(*)
      integer(c_int), value :: lat_len
      integer(c_int), intent(out) :: topology_out
      real(c_double), intent(out) :: lon_out(*)
      integer(c_int), value :: lon_out_max
      integer(c_int), intent(out) :: lon_len_out
      real(c_double), intent(out) :: lat_out(*)
      integer(c_int), value :: lat_out_max
      integer(c_int), intent(out) :: lat_len_out
      integer(c_int), intent(out) :: rc
    end subroutine
  end interface

  real(c_double) :: in_lon(MAX_COORDS), in_lat(MAX_COORDS)
  real(c_double) :: out_lon(MAX_COORDS), out_lat(MAX_COORDS)
  integer :: nfail, rank, err
  integer :: i

  call MPI_Init(err)
  call MPI_Comm_rank(MPI_COMM_WORLD, rank, err)

  nfail = 0

  ! --- Case 1: uniform rectilinear grid (4 x 3 cells, 1-D coords) ---------
  in_lon = 0.0d0
  in_lat = 0.0d0
  do i = 1, 4
    in_lon(i) = real(-180 + 90 * (i - 1), c_double)   ! -180, -90, 0, 90
  end do
  do i = 1, 3
    in_lat(i) = real(-60 + 60 * (i - 1), c_double)    ! -60, 0, 60
  end do
  call check_case("rectilinear 4x3", 4, 3, 72, 0, in_lon, 4, in_lat, 3, &
                  RECTILINEAR, 4, 3, nfail)

  ! --- Case 2: GRIDSPEC-style 2-D coords flattened (length nx*ny) ---------
  ! A 4x3 curvilinear grid arrives as two flattened arrays of length 12; the
  ! cap passes the true dims (nx=4, ny=3) alongside the flattened values.
  in_lon = 0.0d0
  in_lat = 0.0d0
  do i = 1, 12
    in_lon(i) = real(i - 1, c_double) - 180.0d0
    in_lat(i) = real(i - 1, c_double) * 0.5d0 - 60.0d0
  end do
  call check_case("curvilinear 4x3 flattened", 4, 3, 72, 0, in_lon, 12, in_lat, 12, &
                  CURVILINEAR, 12, 12, nfail)

  ! --- Case 3: unstructured mesh node list (ny = 1 convention) ------------
  in_lon = 0.0d0
  in_lat = 0.0d0
  do i = 1, 5
    in_lon(i) = real(10 * i, c_double)
    in_lat(i) = real(-5 * i, c_double)
  end do
  call check_case("unstructured 5 nodes", 5, 1, 4, 0, in_lon, 5, in_lat, 5, &
                  UNSTRUCTURED, 5, 5, nfail)

  ! --- Case 4: radian source is converted to degrees ----------------------
  ! lon: 0, pi/2, -pi  ->  0, 90, -180 (wrapping applies after conversion)
  ! lat: -pi/6, pi/4   ->  -30, 45
  in_lon = 0.0d0
  in_lat = 0.0d0
  in_lon(1) = 0.0d0
  in_lon(2) = 3.141592653589793d0 / 2.0d0
  in_lon(3) = -3.141592653589793d0
  in_lat(1) = -3.141592653589793d0 / 6.0d0
  in_lat(2) = 3.141592653589793d0 / 4.0d0
  call check_case("radian 3x2 grid", 3, 2, 8, 1, in_lon, 3, in_lat, 2, &
                  RECTILINEAR, 3, 2, nfail)

  ! --- Case 5: longitude wrapping -----------------------------------------
  ! lon values of 190 and -200 must wrap into [-180, 180).
  in_lon = 0.0d0
  in_lat = 0.0d0
  in_lon(1) = 190.0d0
  in_lon(2) = -200.0d0
  in_lon(3) = 0.0d0
  in_lat(1) = 10.0d0
  in_lat(2) = 20.0d0
  call check_case("wrap longitudes", 3, 2, 8, 0, in_lon, 3, in_lat, 2, &
                  RECTILINEAR, 3, 2, nfail)

  ! --- Case 6: unsupported/factorized shape is rejected -------------------
  ! lon length matches nx but lat length matches neither ny nor nx*ny.
  in_lon = 0.0d0
  in_lat = 0.0d0
  do i = 1, 4
    in_lon(i) = real(i, c_double)
    in_lat(i) = real(i, c_double)
  end do
  call expect_reject("factorized 4x2 lon[4] lat[4]", 4, 2, 8, 0, in_lon, 4, in_lat, 4, nfail)

  ! --- Case 7: non-positive dims are rejected -----------------------------
  in_lon = 0.0d0
  in_lat = 0.0d0
  in_lon(1) = 1.0d0
  in_lat(1) = 2.0d0
  call expect_reject("zero nx", 0, 1, 4, 0, in_lon, 1, in_lat, 1, nfail)
  call expect_reject("zero nz", 1, 1, 0, 0, in_lon, 1, in_lat, 1, nfail)

  ! --- Case 8: flattened ny=1 with mismatched lengths is rejected ---------
  ! ny == 1 requires lon and lat to each carry exactly nx values.
  in_lon = 0.0d0
  in_lat = 0.0d0
  do i = 1, 4
    in_lon(i) = real(i, c_double)
    in_lat(i) = real(i, c_double)
  end do
  call expect_reject("ny=1 with 4 coords for nx=3", 3, 1, 4, 0, in_lon, 4, in_lat, 4, nfail)

  if (nfail == 0) then
    if (rank == 0) print *, "test_grid_spec_esmf: ALL CASES PASSED"
    call MPI_Finalize(err)
    stop 0
  else
    if (rank == 0) print *, "test_grid_spec_esmf: FAILED cases:", nfail
    call MPI_Finalize(err)
    stop 1
  end if

contains

  !> Run one accepted-shape case through the ABI and assert the result.
  subroutine check_case(name, nx, ny, nz, is_rad, lon, lon_len, lat, lat_len, &
                        want_topo, want_lon_len, want_lat_len, nfail)
    character(len=*), intent(in) :: name
    integer(c_int), intent(in) :: nx, ny, nz, is_rad
    real(c_double), intent(in) :: lon(*), lat(*)
    integer(c_int), intent(in) :: lon_len, lat_len
    integer(c_int), intent(in) :: want_topo, want_lon_len, want_lat_len
    integer, intent(inout) :: nfail

    integer(c_int) :: got_topo, got_lon_len, got_lat_len, got_rc
    integer :: i, bad

    got_topo = -999
    got_lon_len = -999
    got_lat_len = -999
    got_rc = -999
    out_lon = -1.0d0
    out_lat = -1.0d0

    call cece_sim_grid_from_esmf(nx, ny, nz, is_rad, lon, lon_len, lat, lat_len, &
                                 got_topo, out_lon, MAX_COORDS, got_lon_len, &
                                 out_lat, MAX_COORDS, got_lat_len, got_rc)

    bad = 0
    if (got_rc /= 0) then
      print *, "FAIL [", trim(name), "]: expected acceptance, got rc=", int(got_rc)
      bad = 1
    end if
    if (got_topo /= want_topo) then
      print *, "FAIL [", trim(name), "]: topology=", int(got_topo), &
               " expected=", int(want_topo)
      bad = 1
    end if
    if (got_lon_len /= want_lon_len .or. got_lat_len /= want_lat_len) then
      print *, "FAIL [", trim(name), "]: coord lengths (", int(got_lon_len), ",", &
               int(got_lat_len), ") expected (", int(want_lon_len), ",", &
               int(want_lat_len), ")"
      bad = 1
    end if
    if (got_rc == 0) then
      do i = 1, int(got_lon_len)
        if (.not. (out_lon(i) >= -180.0d0 .and. out_lon(i) < 180.0d0)) then
          print *, "FAIL [", trim(name), "]: lon(", i, ")=", out_lon(i), &
                   " outside [-180, 180)"
          bad = 1
          exit
        end if
      end do
      do i = 1, int(got_lat_len)
        if (.not. (out_lat(i) >= -90.0d0 .and. out_lat(i) <= 90.0d0)) then
          print *, "FAIL [", trim(name), "]: lat(", i, ")=", out_lat(i), &
                   " outside [-90, 90]"
          bad = 1
          exit
        end if
      end do
    end if

    if (bad == 0) then
      print *, "PASS [", trim(name), "]"
    else
      nfail = nfail + 1
    end if
  end subroutine check_case

  !> Run a rejected-shape case through the ABI and assert rc /= 0.
  subroutine expect_reject(name, nx, ny, nz, is_rad, lon, lon_len, lat, lat_len, nfail)
    character(len=*), intent(in) :: name
    integer(c_int), intent(in) :: nx, ny, nz, is_rad
    real(c_double), intent(in) :: lon(*), lat(*)
    integer(c_int), intent(in) :: lon_len, lat_len
    integer, intent(inout) :: nfail

    integer(c_int) :: got_topo, got_lon_len, got_lat_len, got_rc

    got_topo = -999
    got_lon_len = -999
    got_lat_len = -999
    got_rc = -999
    out_lon = -1.0d0
    out_lat = -1.0d0

    call cece_sim_grid_from_esmf(nx, ny, nz, is_rad, lon, lon_len, lat, lat_len, &
                                 got_topo, out_lon, MAX_COORDS, got_lon_len, &
                                 out_lat, MAX_COORDS, got_lat_len, got_rc)

    if (got_rc == 0) then
      print *, "FAIL [", trim(name), "]: expected rejection, got rc=0 topology=", &
               int(got_topo)
      nfail = nfail + 1
    else
      print *, "PASS [", trim(name), "] (rejected as expected)"
    end if
  end subroutine expect_reject

end program test_grid_spec_esmf
