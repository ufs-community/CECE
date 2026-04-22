!> @file tide_cf_detection_mod.F90
!> @brief CF Convention Auto-Detection module for the TIDE library.
!>
!> Provides types, error constants, and routines for detecting and interpreting
!> CF (Climate and Forecast) convention metadata in NetCDF files.
module tide_cf_detection_mod
  use shr_kind_mod, only : r8 => shr_kind_r8
  implicit none
  private

  ! -------------------------------------------------------------------------
  ! Error constants
  ! -------------------------------------------------------------------------
  integer, parameter, public :: CF_SUCCESS                =  0
  integer, parameter, public :: CF_ERR_FILE_NOT_FOUND     = -1
  integer, parameter, public :: CF_ERR_FILE_OPEN          = -2
  integer, parameter, public :: CF_ERR_INVALID_FORMAT     = -3
  integer, parameter, public :: CF_ERR_MISSING_ATTRIBUTE  = -4
  integer, parameter, public :: CF_ERR_NO_MATCH           = -5
  integer, parameter, public :: CF_ERR_MULTIPLE_MATCHES   = -6
  integer, parameter, public :: CF_ERR_INCOMPATIBLE_UNITS = -7
  integer, parameter, public :: CF_ERR_INCOMPATIBLE_DIMS  = -8
  integer, parameter, public :: CF_ERR_MISSING_COORDINATES= -9
  integer, parameter, public :: CF_ERR_NON_MONOTONIC      = -10
  integer, parameter, public :: CF_ERR_CACHE_FULL         = -11

  ! -------------------------------------------------------------------------
  ! Public types
  ! -------------------------------------------------------------------------

  !> @brief Metadata for a single NetCDF variable following CF conventions.
  type, public :: cf_variable_metadata_t
    character(len=256) :: var_name          !< NetCDF variable name
    character(len=256) :: standard_name     !< CF standard_name attribute
    character(len=256) :: long_name         !< CF long_name attribute
    character(len=64)  :: units             !< CF units attribute
    character(len=512) :: coordinates       !< CF coordinates attribute
    integer            :: ndims             !< Number of dimensions
    integer            :: dimids(7)         !< Dimension IDs (max 7)
    logical            :: has_standard_name !< True if standard_name present
    logical            :: has_long_name     !< True if long_name present
    logical            :: has_units         !< True if units present
  end type cf_variable_metadata_t

  !> @brief Cache of all CF metadata for a NetCDF file.
  type, public :: cf_metadata_cache_t
    integer :: ncid                                       !< NetCDF file ID (0 if closed)
    character(len=512) :: filename                        !< Full path to NetCDF file
    integer :: nvars                                      !< Number of variables in file
    type(cf_variable_metadata_t), allocatable :: vars(:) !< Variable metadata array
    character(len=64) :: cf_version                       !< CF version string (e.g. "CF-1.8")
    logical :: is_cf_compliant                            !< True if Conventions attr indicates CF
  end type cf_metadata_cache_t

  !> @brief Configuration controlling CF detection behaviour.
  type, public :: cf_detection_config_t
    character(len=16) :: mode          !< "auto", "strict", "disabled", or "coards"
    logical           :: cache_enabled !< Enable metadata caching (default: .true.)
    integer           :: log_level     !< 0=errors, 1=warnings, 2=info, 3=debug
  end type cf_detection_config_t

  !> @brief Result of mapping a model variable to a file variable.
  type, public :: cf_field_mapping_t
    character(len=256) :: model_var               !< Model variable name
    character(len=256) :: file_var                !< File variable name
    character(len=64)  :: source                  !< "cf_standard_name", "cf_long_name", "explicit"
    logical            :: units_compatible         !< True if units are compatible
    logical            :: units_conversion_needed  !< True if unit conversion required
    type(cf_variable_metadata_t) :: metadata      !< Full CF metadata for the matched variable
  end type cf_field_mapping_t

  !> @brief Information about a detected coordinate variable.
  type, public :: cf_coordinate_info_t
    character(len=256) :: var_name      !< Coordinate variable name
    character(len=64)  :: axis          !< "X", "Y", "Z", or "T"
    character(len=256) :: standard_name !< Standard name (e.g. "latitude")
    logical            :: is_monotonic  !< True if values are monotonically ordered
    integer            :: ndim          !< Dimension size
  end type cf_coordinate_info_t

  !> @brief Statistics tracking CF detection performance and results.
  type, public :: cf_detection_stats_t
    integer  :: total_variables          !< Total variables requested
    integer  :: cf_matched               !< Successfully matched via CF
    integer  :: explicit_matched         !< Matched via explicit mapping
    integer  :: failed                   !< Failed to match
    integer  :: unit_conversions_needed  !< Variables requiring unit conversion
    real(r8) :: detection_time_ms        !< Time spent in CF detection (ms)
  end type cf_detection_stats_t

  ! -------------------------------------------------------------------------
  ! Module-level saved state
  ! -------------------------------------------------------------------------
  type(cf_detection_config_t), save :: g_cf_config
  logical, save :: g_cf_initialized = .false.
  integer, save :: g_logunit = 6  !< Default log unit (stdout)

  ! -------------------------------------------------------------------------
  ! Public interface
  ! -------------------------------------------------------------------------
  public :: cf_detection_init
  public :: cf_read_file_metadata
  public :: cf_match_variable
  public :: cf_match_variable_coards
  public :: cf_validate_units
  public :: cf_detect_coordinates
  public :: cf_cache_metadata
  public :: cf_clear_cache
  public :: cf_apply_explicit_mapping
  public :: cf_log

contains

  ! ===========================================================================
  ! Logging helper
  ! ===========================================================================

  !> @brief Write a log message if level <= configured log_level.
  !> @param level   Message severity (0=error, 1=warning, 2=info, 3=debug).
  !> @param message The message string.
  subroutine cf_log(level, message)
    integer,          intent(in) :: level
    character(len=*), intent(in) :: message
    character(len=16) :: prefix
    if (.not. g_cf_initialized) return
    if (level > g_cf_config%log_level) return
    select case (level)
      case (0); prefix = 'ERROR'
      case (1); prefix = 'WARNING'
      case (2); prefix = 'INFO'
      case default; prefix = 'DEBUG'
    end select
    write(g_logunit, '(a,a,a,a)') '[CF-DETECT][', trim(prefix), '] ', trim(message)
  end subroutine cf_log

  ! ===========================================================================
  ! Initialisation
  ! ===========================================================================

  !> @brief Initialise the CF detection engine with the given configuration.
  !> @param config  Detection configuration (mode: auto/strict/disabled/coards, cache_enabled, log_level).
  !> @param rc      Return code: CF_SUCCESS on success.
  subroutine cf_detection_init(config, rc)
    type(cf_detection_config_t), intent(in)  :: config
    integer,                     intent(out) :: rc
    g_cf_config      = config
    g_cf_initialized = .true.
    rc               = CF_SUCCESS
  end subroutine cf_detection_init

  ! ===========================================================================
  ! CF attribute reading via PIO
  ! ===========================================================================

  !> @brief Read all CF convention attributes from a NetCDF file into a cache.
  !>
  !> Note: Full PIO implementation for CF convention metadata auto-detection.
  !>
  !> @param filename      Full path to the NetCDF file.
  !> @param pio_subsystem PIO I/O system descriptor.
  !> @param io_type       PIO I/O type (e.g. PIO_IOTYPE_NETCDF).
  !> @param cache         Output metadata cache populated by this routine.
  !> @param rc            Return code: CF_SUCCESS, or specific CF_ERR_*
  subroutine cf_read_file_metadata(filename, pio_subsystem, io_type, cache, rc)
    use pio, only : iosystem_desc_t, file_desc_t, pio_openfile, pio_closefile, &
                    pio_inq_varid, pio_inquire_variable, pio_inquire_dimension, &
                    pio_get_att, pio_inq_att, pio_inquire, pio_global, pio_noerr, pio_nowrite, &
                    pio_seterrorhandling, pio_bcast_error
    character(len=*),       intent(in)  :: filename
    type(iosystem_desc_t),  intent(inout) :: pio_subsystem
    integer,                intent(in)  :: io_type
    type(cf_metadata_cache_t), intent(out) :: cache
    integer,                intent(out) :: rc

    type(file_desc_t), target :: pio_file
    integer            :: pio_rc, nvars, ivar, varid
    integer            :: old_handle
    character(len=256) :: vname, str_val
    integer            :: att_len
    integer            :: pio_ndims, pio_dimids(7)

    ! Initialize minimal cache
    cache%filename        = trim(filename)
    cache%ncid            = 0  ! Will update if opened successfully
    cache%nvars           = 0  ! no variables detected
    cache%cf_version      = ''
    cache%is_cf_compliant = .false.

    if (.not. g_cf_initialized) then
      call cf_log(0, 'cf_read_file_metadata: cf_detection_init not called')
      rc = CF_ERR_NO_MATCH
      return
    end if

    rc = CF_SUCCESS

    pio_rc = pio_openfile(pio_subsystem, pio_file, io_type, trim(filename), pio_nowrite)
    if (pio_rc /= pio_noerr) then
      call cf_log(0, 'cf_read_file_metadata: Failed to open file: '//trim(filename))
      rc = CF_ERR_FILE_OPEN
      return
    end if

    ! Update cache with ncid
    cache%ncid = pio_file%fh

    ! Set error handling so missing attributes return errors rather than aborting MPI
    call pio_seterrorhandling(pio_file, pio_bcast_error, old_handle)

    ! Check Conventions global attribute
    pio_rc = pio_inq_att(pio_file, pio_global, 'Conventions')
    if (pio_rc == pio_noerr) then
      str_val = ''
      pio_rc = pio_get_att(pio_file, pio_global, 'Conventions', str_val)
      if (pio_rc == pio_noerr) then
        att_len = len_trim(str_val)
        if (att_len > 0) then
          if (index(str_val(1:att_len), 'CF-1.6') > 0 .or. &
              index(str_val(1:att_len), 'CF-1.7') > 0 .or. &
              index(str_val(1:att_len), 'CF-1.8') > 0 .or. &
              index(str_val(1:att_len), 'CF-1.9') > 0) then
            cache%cf_version = trim(str_val(1:att_len))
            cache%is_cf_compliant = .true.
            call cf_log(2, 'cf_read_file_metadata: Detected CF Conventions: '// &
                           trim(str_val(1:att_len))//' for file: '//trim(filename))
          else if (index(str_val(1:att_len), 'COARDS') > 0) then
            cache%cf_version = 'COARDS'
            cache%is_cf_compliant = .false.  ! COARDS-compliant, not CF-compliant
            call cf_log(2, 'cf_read_file_metadata: Detected COARDS Conventions for file: '//trim(filename))
          else
            call cf_log(1, 'cf_read_file_metadata: Non-CF/COARDS Conventions detected: '// &
                           trim(str_val(1:att_len))//' for file: '//trim(filename))
          end if
        end if
      end if
    else
      call cf_log(1, 'cf_read_file_metadata: Missing Conventions global attribute for file: '//trim(filename))
      ! For COARDS mode, assume COARDS compliance even without explicit Conventions attribute
      if (trim(g_cf_config%mode) == 'coards') then
        cache%cf_version = 'COARDS'
        cache%is_cf_compliant = .false.  ! COARDS-compliant, not CF-compliant
        call cf_log(2, 'cf_read_file_metadata: Assuming COARDS convention in coards mode for file: '//trim(filename))
      end if
    end if

    ! Inquire number of variables
    pio_rc = pio_inquire(pio_file, nVariables=nvars)
    if (pio_rc /= pio_noerr) then
      call cf_log(0, 'cf_read_file_metadata: Failed to inquire file: '//trim(filename))
      cache%ncid = 0
      call pio_closefile(pio_file)
      rc = CF_ERR_INVALID_FORMAT
      return
    end if

    cache%nvars = nvars
    if (allocated(cache%vars)) deallocate(cache%vars)
    if (nvars > 0) then
      allocate(cache%vars(nvars))
      do ivar = 1, nvars
        varid = ivar
        pio_rc = pio_inquire_variable(pio_file, varid, name=vname, ndims=pio_ndims, dimids=pio_dimids)
        if (pio_rc == pio_noerr) then
          cache%vars(ivar)%var_name = trim(vname)
          cache%vars(ivar)%ndims = pio_ndims
          if (pio_ndims > 0) then
            cache%vars(ivar)%dimids(1:pio_ndims) = pio_dimids(1:pio_ndims)
          end if

          ! Read standard_name
          cache%vars(ivar)%has_standard_name = .false.
          pio_rc = pio_inq_att(pio_file, varid, 'standard_name')
          if (pio_rc == pio_noerr) then
            str_val = ''
            pio_rc = pio_get_att(pio_file, varid, 'standard_name', str_val)
            if (pio_rc == pio_noerr) then
              att_len = len_trim(str_val)
              if (att_len > 0) then
                cache%vars(ivar)%standard_name = trim(str_val(1:att_len))
                cache%vars(ivar)%has_standard_name = .true.
              end if
            end if
          end if

          ! Read long_name
          cache%vars(ivar)%has_long_name = .false.
          pio_rc = pio_inq_att(pio_file, varid, 'long_name')
          if (pio_rc == pio_noerr) then
            str_val = ''
            pio_rc = pio_get_att(pio_file, varid, 'long_name', str_val)
            if (pio_rc == pio_noerr) then
              att_len = len_trim(str_val)
              if (att_len > 0) then
                cache%vars(ivar)%long_name = trim(str_val(1:att_len))
                cache%vars(ivar)%has_long_name = .true.
              end if
            end if
          end if

          ! Read units
          cache%vars(ivar)%has_units = .false.
          pio_rc = pio_inq_att(pio_file, varid, 'units')
          if (pio_rc == pio_noerr) then
            str_val = ''
            pio_rc = pio_get_att(pio_file, varid, 'units', str_val)
            if (pio_rc == pio_noerr) then
              att_len = len_trim(str_val)
              if (att_len > 0) then
                cache%vars(ivar)%units = trim(str_val(1:att_len))
                cache%vars(ivar)%has_units = .true.
              end if
            end if
          end if

          ! Read coordinates
          cache%vars(ivar)%coordinates = ''
          pio_rc = pio_inq_att(pio_file, varid, 'coordinates')
          if (pio_rc == pio_noerr) then
            str_val = ''
            pio_rc = pio_get_att(pio_file, varid, 'coordinates', str_val)
            if (pio_rc == pio_noerr) then
              att_len = len_trim(str_val)
              if (att_len > 0) then
                cache%vars(ivar)%coordinates = trim(str_val(1:att_len))
              end if
            end if
          end if
        else
          call cf_log(1, 'cf_read_file_metadata: Failed to inquire variable')
        end if
      end do
    end if

    ! Restore old error handling
    call pio_seterrorhandling(pio_file, old_handle)

    call pio_closefile(pio_file)
    cache%ncid = 0

  end subroutine cf_read_file_metadata

  ! ===========================================================================
  ! Standard-name matching (Task 3)
  ! ===========================================================================

  !> @brief Match a model variable to a file variable by CF standard_name.
  !>
  !> Performs case-insensitive, whitespace-normalised comparison of the
  !> model_var string against every standard_name in the cache.  Returns the
  !> first match; logs a warning if multiple matches exist.
  !>
  !> @param model_var  The standard_name to search for.
  !> @param cache      Populated metadata cache.
  !> @param file_var   Output: name of the matched file variable.
  !> @param metadata   Output: full metadata of the matched variable.
  !> @param rc         CF_SUCCESS, CF_ERR_NO_MATCH, or CF_ERR_MULTIPLE_MATCHES.
  subroutine cf_match_variable(model_var, cache, file_var, metadata, rc)
    character(len=*),             intent(in)  :: model_var
    type(cf_metadata_cache_t),    intent(in)  :: cache
    character(len=*),             intent(out) :: file_var
    type(cf_variable_metadata_t), intent(out) :: metadata
    integer,                      intent(out) :: rc

    integer :: ivar, match_idx, match_count
    character(len=256) :: norm_model, norm_std

    rc          = CF_ERR_NO_MATCH
    file_var    = ''
    match_idx   = 0
    match_count = 0

    norm_model = cf_normalize_string(model_var)

    do ivar = 1, cache%nvars
      if (.not. cache%vars(ivar)%has_standard_name) cycle
      norm_std = cf_normalize_string(cache%vars(ivar)%standard_name)
      if (trim(norm_std) == trim(norm_model)) then
        match_count = match_count + 1
        if (match_count == 1) match_idx = ivar
      end if
    end do

    if (match_count == 0) then
      rc = CF_ERR_NO_MATCH
      return
    end if

    if (match_count > 1) then
      call cf_log(1, 'Multiple variables match standard_name "'//trim(model_var)//'" — using first match')
      rc = CF_ERR_MULTIPLE_MATCHES
    else
      rc = CF_SUCCESS
    end if

    file_var = trim(cache%vars(match_idx)%var_name)
    metadata = cache%vars(match_idx)
    metadata%standard_name = trim(cache%vars(match_idx)%standard_name)

    ! Log success
    call cf_log(2, 'CF match: standard_name="'//trim(model_var)//'" -> file_var="'//trim(file_var)//'"')

    ! Treat multiple-match as success (first match used)
    if (rc == CF_ERR_MULTIPLE_MATCHES) rc = CF_SUCCESS

  end subroutine cf_match_variable

  !> @brief Match a model variable to a file variable using COARDS conventions.
  !>
  !> COARDS uses simpler variable name matching without requiring standard_name.
  !> This is used as a fallback when CF matching fails in "auto" mode or as the
  !> primary method in "coards" mode.
  !>
  !> @param model_var  The model variable name to search for.
  !> @param cache      Populated metadata cache.
  !> @param file_var   Output: name of the matched file variable.
  !> @param metadata   Output: full metadata of the matched variable.
  !> @param rc         CF_SUCCESS, CF_ERR_NO_MATCH, or CF_ERR_MULTIPLE_MATCHES.
  subroutine cf_match_variable_coards(model_var, cache, file_var, metadata, rc)
    character(len=*),             intent(in)  :: model_var
    type(cf_metadata_cache_t),    intent(in)  :: cache
    character(len=*),             intent(out) :: file_var
    type(cf_variable_metadata_t), intent(out) :: metadata
    integer,                      intent(out) :: rc

    integer :: ivar, match_idx, match_count
    character(len=256) :: norm_model, norm_var

    rc          = CF_ERR_NO_MATCH
    file_var    = ''
    match_idx   = 0
    match_count = 0

    norm_model = cf_normalize_string(model_var)

    ! Try exact variable name matching (COARDS style)
    do ivar = 1, cache%nvars
      norm_var = cf_normalize_string(cache%vars(ivar)%var_name)
      if (trim(norm_var) == trim(norm_model)) then
        match_count = match_count + 1
        if (match_count == 1) match_idx = ivar
      end if
    end do

    if (match_count == 0) then
      rc = CF_ERR_NO_MATCH
      call cf_log(2, 'COARDS matching failed for: "'//trim(model_var)//'" - no variable name match')
      return
    end if

    if (match_count > 1) then
      call cf_log(1, 'Multiple variables match name "'//trim(model_var)//'" — using first match')
      rc = CF_ERR_MULTIPLE_MATCHES
    else
      rc = CF_SUCCESS
    end if

    ! Return the matched variable
    file_var = trim(cache%vars(match_idx)%var_name)
    metadata = cache%vars(match_idx)

    call cf_log(2, 'COARDS match successful')

  end subroutine cf_match_variable_coards

  ! ===========================================================================
  ! Unit validation (Task 5)
  ! ===========================================================================

  !> @brief Compare file and model units for compatibility.
  !>
  !> Identical strings → no conversion needed.
  !> Different but dimensionally equivalent strings → conversion needed, warning.
  !> Incompatible → error, Field_Map rejected.
  !>
  !> @param file_units        Units string from the NetCDF file.
  !> @param model_units       Units string expected by the model.
  !> @param compatible        .true. if units are compatible.
  !> @param conversion_needed .true. if a unit conversion is required.
  !> @param rc                CF_SUCCESS or CF_ERR_INCOMPATIBLE_UNITS.
  subroutine cf_validate_units(file_units, model_units, compatible, conversion_needed, rc)
    character(len=*), intent(in)  :: file_units, model_units
    logical,          intent(out) :: compatible
    logical,          intent(out) :: conversion_needed
    integer,          intent(out) :: rc

    character(len=64) :: fu, mu

    compatible        = .false.
    conversion_needed = .false.
    rc                = CF_SUCCESS

    fu = adjustl(trim(file_units))
    mu = adjustl(trim(model_units))

    ! Identical units — no conversion needed
    if (fu == mu) then
      compatible        = .true.
      conversion_needed = .false.
      return
    end if

    ! Check for known dimensionally-equivalent pairs
    if (cf_units_equivalent(fu, mu)) then
      compatible        = .true.
      conversion_needed = .true.
      call cf_log(1, 'Unit conversion needed: file="'//trim(fu)//'" model="'//trim(mu)//'"')
      return
    end if

    ! Incompatible units
    compatible        = .false.
    conversion_needed = .false.
    rc                = CF_ERR_INCOMPATIBLE_UNITS
    call cf_log(0, 'Incompatible units: file="'//trim(fu)//'" model="'//trim(mu)//'"')

  end subroutine cf_validate_units

  ! ===========================================================================
  ! Coordinate variable detection (Task 6)
  ! ===========================================================================

  !> @brief Identify coordinate variables in the metadata cache.
  !>
  !> Recognises variables by standard coordinate names (lat, latitude, lon,
  !> longitude, time, lev, level) or by their standard_name attribute.
  !>
  !> @param cache      Populated metadata cache.
  !> @param coord_vars Output: array of detected coordinate variable metadata.
  !> @param rc         CF_SUCCESS or CF_ERR_MISSING_COORDINATES.
  subroutine cf_detect_coordinates(cache, coord_vars, rc)
    type(cf_metadata_cache_t),                intent(in)  :: cache
    type(cf_coordinate_info_t), allocatable,  intent(out) :: coord_vars(:)
    integer,                                  intent(out) :: rc

    integer :: ivar, ncoord, i
    logical :: is_coord
    character(len=256) :: vname, sname
    type(cf_coordinate_info_t) :: tmp(cache%nvars)

    rc     = CF_SUCCESS
    ncoord = 0

    do ivar = 1, cache%nvars
      vname    = trim(cache%vars(ivar)%var_name)
      sname    = trim(cache%vars(ivar)%standard_name)
      is_coord = .false.

      ! Check by variable name
      if (cf_is_coord_name(vname)) is_coord = .true.

      ! Check by standard_name
      if (.not. is_coord .and. cache%vars(ivar)%has_standard_name) then
        if (cf_is_coord_standard_name(sname)) is_coord = .true.
      end if

      if (is_coord) then
        ncoord = ncoord + 1
        tmp(ncoord)%var_name      = trim(vname)
        tmp(ncoord)%standard_name = trim(sname)
        tmp(ncoord)%axis          = cf_coord_axis(vname, sname)
        tmp(ncoord)%is_monotonic  = .true.  ! validated separately
        tmp(ncoord)%ndim          = 0
      end if
    end do

    if (ncoord == 0) then
      call cf_log(0, 'No coordinate variables found in file '//trim(cache%filename))
      rc = CF_ERR_MISSING_COORDINATES
      return
    end if

    allocate(coord_vars(ncoord))
    do i = 1, ncoord
      coord_vars(i) = tmp(i)
    end do

  end subroutine cf_detect_coordinates

  ! ===========================================================================
  ! Metadata caching (Task 7)
  ! ===========================================================================

  !> @brief Store a populated cache (no-op if cache already has same filename).
  !> @param cache  The cache to store/validate.
  !> @param rc     CF_SUCCESS always.
  subroutine cf_cache_metadata(cache, rc)
    type(cf_metadata_cache_t), intent(inout) :: cache
    integer,                   intent(out)   :: rc
    rc = CF_SUCCESS
    ! The cache is the storage itself; this routine is a hook for future
    ! multi-file cache management.
    call cf_log(3, 'cf_cache_metadata: cached '//trim(cache%filename))
  end subroutine cf_cache_metadata

  !> @brief Clear a metadata cache, deallocating the variable array.
  !> @param cache  The cache to clear.
  subroutine cf_clear_cache(cache)
    type(cf_metadata_cache_t), intent(inout) :: cache
    if (allocated(cache%vars)) deallocate(cache%vars)
    cache%nvars         = 0
    cache%ncid          = 0
    cache%filename      = ''
    cache%cf_version    = ''
    cache%is_cf_compliant = .false.
    call cf_log(3, 'cf_clear_cache: cache cleared')
  end subroutine cf_clear_cache

  ! ===========================================================================
  ! Explicit mapping fallback (Task 12)
  ! ===========================================================================

  !> @brief Apply explicit file_var/model_var mapping from YAML configuration.
  !>
  !> When explicit mapping is present it takes priority over CF detection
  !> (Requirement 4.2).  Returns CF_ERR_NO_MATCH only when both CF and
  !> explicit mapping fail.
  !>
  !> @param model_var       The model variable name to resolve.
  !> @param fld_list_file   Array of explicit file variable names from YAML.
  !> @param fld_list_model  Array of explicit model variable names from YAML.
  !> @param nfields         Number of entries in the field lists.
  !> @param file_var        Output: resolved file variable name.
  !> @param rc              CF_SUCCESS or CF_ERR_NO_MATCH.
  subroutine cf_apply_explicit_mapping(model_var, fld_list_file, fld_list_model, &
                                       nfields, file_var, rc)
    character(len=*), intent(in)  :: model_var
    character(len=*), intent(in)  :: fld_list_file(:)
    character(len=*), intent(in)  :: fld_list_model(:)
    integer,          intent(in)  :: nfields
    character(len=*), intent(out) :: file_var
    integer,          intent(out) :: rc

    integer :: j

    rc       = CF_ERR_NO_MATCH
    file_var = ''

    do j = 1, nfields
      if (trim(fld_list_model(j)) == trim(model_var)) then
        file_var = trim(fld_list_file(j))
        rc       = CF_SUCCESS
        call cf_log(2, 'Explicit mapping: model_var="'//trim(model_var)// &
                       '" -> file_var="'//trim(file_var)//'"')
        return
      end if
    end do

    call cf_log(1, 'No explicit mapping found for model_var="'//trim(model_var)//'"')

  end subroutine cf_apply_explicit_mapping

  ! ===========================================================================
  ! Private helpers
  ! ===========================================================================

  !> @brief Normalise a string: lowercase and collapse internal whitespace.
  !> @param s The string to normalise.
  !> @return The normalised string.
  pure function cf_normalize_string(s) result(out)
    character(len=*), intent(in) :: s
    character(len=256) :: out
    integer :: i, j, ic
    logical :: prev_space
    out        = ''
    j          = 0
    prev_space = .true.  ! trim leading spaces

    ! Iterate through characters, lowercasing and trimming spaces
    do i = 1, len_trim(s)
      ic = iachar(s(i:i))
      if (ic == 32 .or. ic == 9) then  ! space or tab
        if (.not. prev_space) then
          j = j + 1
          out(j:j) = ' '
          prev_space = .true.
        end if
      else
        ! lowercase A-Z
        if (ic >= 65 .and. ic <= 90) ic = ic + 32
        j = j + 1
        out(j:j) = achar(ic)
        prev_space = .false.
      end if
    end do
    ! trim trailing space added above
    out = trim(out)
  end function cf_normalize_string

  !> @brief Return .true. if the variable name is a standard coordinate name.
  !> @param vname The variable name to check.
  !> @return True if it is a coordinate name, false otherwise.
  pure function cf_is_coord_name(vname) result(res)
    character(len=*), intent(in) :: vname
    logical :: res
    character(len=256) :: n
    n = cf_normalize_string(vname)
    res = (trim(n) == 'lat'       .or. trim(n) == 'latitude'  .or. &
           trim(n) == 'lon'       .or. trim(n) == 'longitude' .or. &
           trim(n) == 'time'      .or. &
           trim(n) == 'lev'       .or. trim(n) == 'level')
  end function cf_is_coord_name

  !> @brief Return .true. if the standard_name identifies a coordinate variable.
  !> @param sname The standard_name to check.
  !> @return True if it is a standard coordinate name, false otherwise.
  pure function cf_is_coord_standard_name(sname) result(res)
    character(len=*), intent(in) :: sname
    logical :: res
    character(len=256) :: n
    n = cf_normalize_string(sname)
    res = (trim(n) == 'latitude'  .or. trim(n) == 'longitude' .or. &
           trim(n) == 'time'      .or. trim(n) == 'altitude'  .or. &
           trim(n) == 'height'    .or. trim(n) == 'air_pressure' .or. &
           trim(n) == 'atmosphere_sigma_coordinate')
  end function cf_is_coord_standard_name

  !> @brief Return the axis label ("X","Y","Z","T") for a coordinate variable.
  !> @param vname The variable name.
  !> @param sname The standard name.
  !> @return The axis label ("X","Y","Z","T" or empty string if not matched).
  pure function cf_coord_axis(vname, sname) result(axis)
    character(len=*), intent(in) :: vname, sname
    character(len=64) :: axis
    character(len=256) :: n
    n = cf_normalize_string(vname)
    if (trim(n) == 'lat' .or. trim(n) == 'latitude') then
      axis = 'Y'
    else if (trim(n) == 'lon' .or. trim(n) == 'longitude') then
      axis = 'X'
    else if (trim(n) == 'time') then
      axis = 'T'
    else if (trim(n) == 'lev' .or. trim(n) == 'level') then
      axis = 'Z'
    else
      n = cf_normalize_string(sname)
      if (trim(n) == 'latitude')  then; axis = 'Y'
      else if (trim(n) == 'longitude') then; axis = 'X'
      else if (trim(n) == 'time')      then; axis = 'T'
      else if (trim(n) == 'altitude' .or. trim(n) == 'height') then; axis = 'Z'
      else; axis = ''
      end if
    end if
  end function cf_coord_axis

  !> @brief Return .true. if two unit strings are dimensionally equivalent.
  !>
  !> This is a lightweight check covering common emission-inventory unit pairs.
  !> A full UDUNITS-2 integration would replace this in production.
  !> @param u1 The first unit string.
  !> @param u2 The second unit string.
  !> @return True if the unit strings are dimensionally equivalent, false otherwise.
  pure function cf_units_equivalent(u1, u2) result(res)
    character(len=*), intent(in) :: u1, u2
    logical :: res
    ! Mass-flux equivalences (kg vs g, per-area, per-time)
    res = .false.
    if ((trim(u1) == 'kg m-2 s-1' .and. trim(u2) == 'g m-2 s-1') .or. &
        (trim(u1) == 'g m-2 s-1'  .and. trim(u2) == 'kg m-2 s-1')) then
      res = .true.; return
    end if
    if ((trim(u1) == 'kg/m2/s'    .and. trim(u2) == 'g/m2/s') .or. &
        (trim(u1) == 'g/m2/s'     .and. trim(u2) == 'kg/m2/s')) then
      res = .true.; return
    end if
    if ((trim(u1) == 'mol m-2 s-1' .and. trim(u2) == 'mmol m-2 s-1') .or. &
        (trim(u1) == 'mmol m-2 s-1'.and. trim(u2) == 'mol m-2 s-1')) then
      res = .true.; return
    end if
    ! Temperature equivalences
    if ((trim(u1) == 'K'  .and. trim(u2) == 'degC') .or. &
        (trim(u1) == 'degC' .and. trim(u2) == 'K')) then
      res = .true.; return
    end if
  end function cf_units_equivalent

end module tide_cf_detection_mod
