! Shared ISO_C_BINDING adapter. No ingestion, driver clock, or MPI initialization.
module cece_scheme_bindings
  use iso_c_binding
  implicit none
  private
  public :: evaluate_scheme
  interface
    integer(c_int) function create_c(name, options, ncol, nlev, handle, error, cap) &
        bind(C, name='cece_scheme_create')
      import
      character(c_char), intent(in) :: name(*), options(*)
      integer(c_int), value :: ncol, nlev, cap
      integer(c_int64_t), intent(out) :: handle
      character(c_char), intent(out) :: error(*)
    end function
    integer(c_int) function set_c(handle, name, data, count, extent, error, cap) &
        bind(C, name='cece_scheme_set_field')
      import
      integer(c_int64_t), value :: handle
      character(c_char), intent(in) :: name(*)
      real(c_double), intent(in) :: data(*)
      integer(c_size_t), value :: count
      integer(c_int), value :: extent, cap
      character(c_char), intent(out) :: error(*)
    end function
    integer(c_int) function step_c(handle, time, dt, error, cap) bind(C, name='cece_scheme_step')
      import
      integer(c_int64_t), value :: handle
      real(c_double), value :: time, dt
      integer(c_int), value :: cap
      character(c_char), intent(out) :: error(*)
    end function
    integer(c_int) function get_c(handle, name, data, count, error, cap) bind(C, name='cece_scheme_get_field')
      import
      integer(c_int64_t), value :: handle
      character(c_char), intent(in) :: name(*)
      real(c_double), intent(out) :: data(*)
      integer(c_size_t), value :: count
      integer(c_int), value :: cap
      character(c_char), intent(out) :: error(*)
    end function
    integer(c_int) function destroy_c(handle, error, cap) bind(C, name='cece_scheme_destroy')
      import
      integer(c_int64_t), value :: handle
      integer(c_int), value :: cap
      character(c_char), intent(out) :: error(*)
    end function
    integer(c_int) function count_c(handle, count, error, cap) bind(C, name='cece_scheme_output_count')
      import
      integer(c_int64_t), value :: handle
      integer(c_int), intent(out) :: count
      integer(c_int), value :: cap
      character(c_char), intent(out) :: error(*)
    end function
    integer(c_int) function name_c(handle, index, name, name_cap, error, cap) &
        bind(C, name='cece_scheme_output_name')
      import
      integer(c_int64_t), value :: handle
      integer(c_int), value :: index, name_cap, cap
      character(c_char), intent(out) :: name(*), error(*)
    end function
  end interface
contains
  subroutine copy_error(text,error)
    character(len=*), intent(in) :: text
    character(c_char), intent(out) :: error(:)
    integer :: i
    error=c_null_char
    do i=1,min(len_trim(text),size(error)-1)
      error(i)=text(i:i)
    end do
  end subroutine
  subroutine evaluate_scheme(name, options, ncol, nlev, extent, input_names, inputs, &
                             output_names, outputs, time, dt, errmsg, errflg)
    character(len=*), intent(in) :: name, options
    integer, intent(in) :: ncol, nlev, extent
    character(len=*), intent(in) :: input_names(:), output_names(:)
    real(c_double), intent(in) :: inputs(:,:), time, dt
    real(c_double), intent(inout) :: outputs(:,:)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    integer(c_int64_t) :: handle
    integer(c_int) :: rc, cleanup_rc, count
    character(c_char) :: error(512), cleanup_error(512), actual_name(128)
    character(len=128) :: actual
    real(c_double), allocatable :: staged(:,:)
    integer :: i, j
    errmsg = ''
    errflg = 0
    if (ncol <= 0 .or. nlev <= 0 .or. extent <= 0) then
      errmsg = 'CECE requires positive column/axis extents'
      errflg = 1
      return
    end if
    if (size(inputs,1) /= ncol*extent .or. size(inputs,2) /= size(input_names) .or. &
        size(outputs,1) /= ncol*extent .or. size(outputs,2) /= size(output_names)) then
      errmsg = 'CECE cap array dimensions do not match the field contract'
      errflg = 1
      return
    end if
    ! Current reviewed production kernels are stateless with explicit effective
    ! histories. A call-local context avoids hidden block/thread identity. It
    ! uses the same runtime as Python; future prognostic kernels need explicit
    ! host state transfer before they can use this adapter.
    rc = create_c(trim(name)//c_null_char, trim(options)//c_null_char, &
                  int(ncol,c_int), int(nlev,c_int), handle, error, 512_c_int)
    if (rc == 0) then
      rc = count_c(handle, count, error, 512_c_int)
      if (rc == 0 .and. count /= size(output_names)) then
        rc = -1
        error = c_null_char
        call copy_error('CECE output species count mismatch',error)
      end if
      if (rc == 0) then
        do i = 1, count
          rc = name_c(handle,int(i-1,c_int),actual_name,128_c_int,error,512_c_int)
          if (rc /= 0) exit
          actual=''
          do j=1,128
            if (actual_name(j)==c_null_char) exit
            actual(j:j)=actual_name(j)
          end do
          if (trim(actual)/=trim(output_names(i))) then
            rc=-1
            error=c_null_char
            call copy_error('CECE species manifest order mismatch',error)
            exit
          end if
        end do
      end if
      do i = 1, size(input_names)
        if (rc /= 0) exit
        rc = set_c(handle, trim(input_names(i))//c_null_char, inputs(:,i), &
                   int(ncol,c_size_t)*int(extent,c_size_t), int(extent,c_int), error, 512_c_int)
        if (rc /= 0) exit
      end do
      if (rc == 0) rc = step_c(handle, time, dt, error, 512_c_int)
      if (rc == 0) then
        allocate(staged(size(outputs,1),size(outputs,2)))
        do i = 1, size(output_names)
          rc = get_c(handle, trim(output_names(i))//c_null_char, staged(:,i), &
                     int(size(outputs,1),c_size_t), error, 512_c_int)
          if (rc /= 0) exit
        end do
      end if
      cleanup_rc = destroy_c(handle, cleanup_error, 512_c_int)
      if (rc == 0 .and. cleanup_rc /= 0) then
        rc = cleanup_rc
        error = cleanup_error
      end if
    end if
    errflg = int(rc)
    if (rc /= 0) then
      do j = 1, min(len(errmsg),512)
        if (error(j) == c_null_char) exit
        errmsg(j:j) = error(j)
      end do
    else
      outputs = staged
    end if
  end subroutine
end module
