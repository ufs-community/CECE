!> @file iso8601_utils.F90
!> @brief ISO8601 datetime parsing and formatting utilities
!> @details Provides functions to parse and format ISO8601 datetime strings
!>          for use with ESMF_Time objects.

module iso8601_utils
  use ESMF
  use, intrinsic :: iso_c_binding
  implicit none

  private
  public :: parse_iso8601_to_esmf_time
  public :: format_esmf_time_to_iso8601
  public :: parse_iso8601_to_esmf_time_c_wrapper
  public :: format_esmf_time_to_iso8601_c_wrapper
  public :: init_iso8601_utils

  ! Module-level calendar (must be initialized before use)
  type(ESMF_Calendar), save :: gregorian_calendar
  logical, save :: calendar_initialized = .false.

contains

  !> @brief Initialize the ISO8601 utilities (must be called after ESMF_Initialize)
  subroutine init_iso8601_utils()
    integer :: rc

    if (.not. calendar_initialized) then
      gregorian_calendar = ESMF_CalendarCreate(ESMF_CALKIND_GREGORIAN, name="Gregorian", rc=rc)
      if (rc == ESMF_SUCCESS) then
        calendar_initialized = .true.
      else
        write(*,'(A)') "ERROR: [init_iso8601_utils] Failed to create calendar"
      end if
    end if
  end subroutine init_iso8601_utils

  !> @brief Parse ISO8601 datetime string to ESMF_Time
  !> @param iso_str Input ISO8601 datetime string (YYYY-MM-DDTHH:MM:SS)
  !> @param time_out Output ESMF_Time object
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine parse_iso8601_to_esmf_time(iso_str, time_out, rc)
    character(len=*), intent(in) :: iso_str
    type(ESMF_Time), intent(out) :: time_out
    integer, intent(out) :: rc

    integer :: yy, mm, dd, hh, mn, ss
    integer :: str_len, iostat
    character(len=256) :: err_msg

    ! Ensure calendar is initialized
    if (.not. calendar_initialized) then
      call init_iso8601_utils()
      if (.not. calendar_initialized) then
        rc = ESMF_FAILURE
        return
      end if
    end if

    rc = ESMF_SUCCESS

    ! Validate string length (must be at least 19 characters for YYYY-MM-DDTHH:MM:SS)
    str_len = len_trim(iso_str)
    if (str_len < 19) then
      write(err_msg, '(A,I0,A)') "ERROR: [parse_iso8601] Invalid ISO8601 format - string too short (", &
                                 str_len, " chars, expected 19)"
      write(*,'(A)') trim(err_msg)
      rc = ESMF_FAILURE
      return
    end if

    ! Validate format separators
    if (iso_str(5:5) /= '-' .or. iso_str(8:8) /= '-' .or. &
        iso_str(11:11) /= 'T' .or. iso_str(14:14) /= ':' .or. &
        iso_str(17:17) /= ':') then
      write(*,'(A)') "ERROR: [parse_iso8601] Invalid ISO8601 format - incorrect separators"
      write(*,'(A,A)') "  Expected: YYYY-MM-DDTHH:MM:SS, got: ", trim(iso_str)
      rc = ESMF_FAILURE
      return
    end if

    ! Parse year
    read(iso_str(1:4), '(I4)', iostat=iostat) yy
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse year"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse month
    read(iso_str(6:7), '(I2)', iostat=iostat) mm
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse month"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse day
    read(iso_str(9:10), '(I2)', iostat=iostat) dd
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse day"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse hour
    read(iso_str(12:13), '(I2)', iostat=iostat) hh
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse hour"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse minute
    read(iso_str(15:16), '(I2)', iostat=iostat) mn
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse minute"
      rc = ESMF_FAILURE
      return
    end if

    ! Parse second
    read(iso_str(18:19), '(I2)', iostat=iostat) ss
    if (iostat /= 0) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to parse second"
      rc = ESMF_FAILURE
      return
    end if

    ! Validate ranges
    if (mm < 1 .or. mm > 12) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid month: ", mm
      rc = ESMF_FAILURE
      return
    end if

    if (dd < 1 .or. dd > 31) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid day: ", dd
      rc = ESMF_FAILURE
      return
    end if

    if (hh < 0 .or. hh > 23) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid hour: ", hh
      rc = ESMF_FAILURE
      return
    end if

    if (mn < 0 .or. mn > 59) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid minute: ", mn
      rc = ESMF_FAILURE
      return
    end if

    if (ss < 0 .or. ss > 59) then
      write(*,'(A,I0)') "ERROR: [parse_iso8601] Invalid second: ", ss
      rc = ESMF_FAILURE
      return
    end if

    ! Create ESMF_Time object with calendar
    call ESMF_TimeSet(time_out, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, calendar=gregorian_calendar, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [parse_iso8601] Failed to create ESMF_Time"
      write(*,'(A,I4,A,I2,A,I2,A,I2,A,I2,A,I2)') &
        "  Date/time: ", yy, "-", mm, "-", dd, " ", hh, ":", mn, ":", ss
      return
    end if

  end subroutine parse_iso8601_to_esmf_time

  !> @brief Format ESMF_Time to ISO8601 string
  !> @param time_in Input ESMF_Time object
  !> @param iso_str Output ISO8601 datetime string (must be at least 19 characters)
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine format_esmf_time_to_iso8601(time_in, iso_str, rc)
    type(ESMF_Time), intent(in) :: time_in
    character(len=*), intent(out) :: iso_str
    integer, intent(out) :: rc

    integer :: yy, mm, dd, hh, mn, ss

    rc = ESMF_SUCCESS

    ! Extract components from ESMF_Time
    call ESMF_TimeGet(time_in, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [format_esmf_time_to_iso8601] Failed to extract time components"
      return
    end if

    ! Format as ISO8601 string
    write(iso_str, '(I4.4,A,I2.2,A,I2.2,A,I2.2,A,I2.2,A,I2.2)') &
      yy, '-', mm, '-', dd, 'T', hh, ':', mn, ':', ss

  end subroutine format_esmf_time_to_iso8601

  !> @brief C wrapper for parse_iso8601_to_esmf_time (for testing)
  !> @param iso_str Input ISO8601 datetime string (null-terminated C string)
  !> @param yy Output year
  !> @param mm Output month
  !> @param dd Output day
  !> @param hh Output hour
  !> @param mn Output minute
  !> @param ss Output second
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine parse_iso8601_to_esmf_time_c_wrapper(iso_str, yy, mm, dd, hh, mn, ss, rc) &
    bind(C, name="parse_iso8601_to_esmf_time_c_wrapper")
    character(kind=c_char), dimension(*), intent(in) :: iso_str
    integer(c_int), intent(out) :: yy, mm, dd, hh, mn, ss, rc

    type(ESMF_Time) :: time_out
    character(len=256) :: fortran_str
    integer :: i, str_len

    ! Convert C string to Fortran string
    str_len = 0
    do i = 1, 256
      if (iso_str(i) == c_null_char) exit
      fortran_str(i:i) = iso_str(i)
      str_len = i
    end do

    ! Parse ISO8601 string
    call parse_iso8601_to_esmf_time(fortran_str(1:str_len), time_out, rc)
    if (rc /= ESMF_SUCCESS) then
      return
    end if

    ! Extract components from ESMF_Time
    call ESMF_TimeGet(time_out, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, rc=rc)

  end subroutine parse_iso8601_to_esmf_time_c_wrapper

  !> @brief C wrapper for format_esmf_time_to_iso8601 (for testing)
  !> @param yy Input year
  !> @param mm Input month
  !> @param dd Input day
  !> @param hh Input hour
  !> @param mn Input minute
  !> @param ss Input second
  !> @param iso_str Output ISO8601 datetime string (null-terminated C string)
  !> @param rc Return code (ESMF_SUCCESS on success, ESMF_FAILURE on error)
  subroutine format_esmf_time_to_iso8601_c_wrapper(yy, mm, dd, hh, mn, ss, iso_str, rc) &
    bind(C, name="format_esmf_time_to_iso8601_c_wrapper")
    integer(c_int), intent(in) :: yy, mm, dd, hh, mn, ss
    character(kind=c_char), dimension(*), intent(out) :: iso_str
    integer(c_int), intent(out) :: rc

    type(ESMF_Time) :: time_in
    character(len=256) :: fortran_str
    integer :: i

    ! Ensure calendar is initialized
    if (.not. calendar_initialized) then
      call init_iso8601_utils()
      if (.not. calendar_initialized) then
        rc = ESMF_FAILURE
        return
      end if
    end if

    ! Create ESMF_Time from components with calendar
    call ESMF_TimeSet(time_in, yy=yy, mm=mm, dd=dd, &
                      h=hh, m=mn, s=ss, calendar=gregorian_calendar, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,'(A)') "ERROR: [format_esmf_time_to_iso8601_c_wrapper] Failed to create ESMF_Time"
      return
    end if

    ! Format to ISO8601 string
    call format_esmf_time_to_iso8601(time_in, fortran_str, rc)
    if (rc /= ESMF_SUCCESS) then
      return
    end if

    ! Convert Fortran string to C string (null-terminated)
    do i = 1, len_trim(fortran_str)
      iso_str(i) = fortran_str(i:i)
    end do
    iso_str(len_trim(fortran_str) + 1) = c_null_char

  end subroutine format_esmf_time_to_iso8601_c_wrapper

  !> @brief C wrapper for init_iso8601_utils (for testing)
  subroutine init_iso8601_utils_c_wrapper() bind(C, name="init_iso8601_utils_c_wrapper")
    call init_iso8601_utils()
  end subroutine init_iso8601_utils_c_wrapper

end module iso8601_utils
