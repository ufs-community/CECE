program test_grid_create
  use ESMF
  implicit none
  type(ESMF_Grid) :: grid
  integer :: rc
  character(len=256) :: filename

  filename = "data/MACCity_4x5.nc"

  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) print *, "ESMF_Initialize failed"

  print *, "Attempting GRIDSPEC creation..."
  grid = ESMF_GridCreate(trim(filename), fileformat=ESMF_FILEFORMAT_GRIDSPEC, &
       isSphere=.true., addUserArea=.true., addMask=.true., rc=rc)
  print *, "Return code: ", rc

  if (rc /= ESMF_SUCCESS) then
     print *, "Attempting SCRIP creation..."
     grid = ESMF_GridCreate(trim(filename), fileformat=ESMF_FILEFORMAT_SCRIP, &
          rc=rc)
     print *, "Return code: ", rc
  endif

  call ESMF_Finalize(rc=rc)
end program test_grid_create
