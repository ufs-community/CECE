!> @file tide_mod.F90
!> @brief High-level API for the TIDE library.
module tide_mod
  use tide_yaml_mod
  use tide_cf_detection_mod  ! Add CF detection support for Task 13
  use dshr_strdata_mod
  use ESMF
  use pio
  use, intrinsic :: iso_c_binding
  use shr_kind_mod, only : r8 => shr_kind_r8, cl => shr_kind_cl, cs => shr_kind_cs
  implicit none

  !> @brief TIDE handle type containing stream data information.
  type tide_type
    type(shr_strdata_type), allocatable :: sdat(:) !< Core stream data structures (one per stream)
    integer :: num_streams !< Number of streams
    integer :: year_first, year_last
  end type tide_type

  !> @brief PIO subsystem for standalone TIDE usage
  type(iosystem_desc_t), target, save :: tide_io_system
  logical, save :: tide_pio_initialized = .false.

contains

  !> @brief Initializes the TIDE library from a YAML configuration.
  !> @param tide The TIDE handle to initialize.
  !> @param config_yaml Path to the YAML configuration file.
  !> @param model_mesh The ESMF Mesh of the model.
  !> @param clock The model's ESMF Clock.
  !> @param rc Return code (ESMF_SUCCESS or ESMF_FAILURE).
  subroutine tide_init(tide, config_yaml, model_mesh, clock, rc)
    use dshr_strdata_mod, only : shr_strdata_init_from_inline
    type(tide_type), intent(inout) :: tide
    character(len=*), intent(in) :: config_yaml
    type(ESMF_Mesh), intent(in) :: model_mesh
    type(ESMF_Clock), intent(in) :: clock
    integer, intent(out) :: rc

    type(c_ptr) :: c_cfg_ptr
    type(tide_config_t), pointer :: cfg
    type(tide_stream_config_t), pointer :: s_cfg_ptr(:)
    integer :: i, j
    integer :: total_files, total_fields, file_idx, field_idx
    character(len=1024) :: c_str
    character(kind=c_char), target :: c_config_yaml(len(trim(config_yaml))+1)
    character(len=cl), allocatable :: file_names(:)
    character(len=cl), allocatable :: fld_list_file(:)
    character(len=cl), allocatable :: fld_list_model(:)
    type(c_ptr), pointer :: input_files_ptr(:)
    type(c_ptr), pointer :: file_vars_ptr(:)
    type(c_ptr), pointer :: model_vars_ptr(:)
    integer :: my_task, n_tasks, comm
    type(ESMF_VM) :: vm
    character(len=cl) :: mesh_file, tax_mode, time_interp, map_algo

    rc = ESMF_SUCCESS

    ! Parse YAML
    do i = 1, len(trim(config_yaml))
      c_config_yaml(i) = config_yaml(i:i)
    end do
    c_config_yaml(len(trim(config_yaml))+1) = c_null_char

    ! Parse YAML configuration file
    c_cfg_ptr = tide_parse_yaml(c_loc(c_config_yaml))
    if (.not. c_associated(c_cfg_ptr)) then
      write(*,*) "ERROR: [TIDE] Failed to parse YAML configuration file: ", trim(config_yaml)
      rc = ESMF_FAILURE
      return
    end if
    call c_f_pointer(c_cfg_ptr, cfg)

    if (cfg%num_streams < 1) then
      write(*,*) "WARNING: [TIDE] No streams found in YAML configuration file: ", trim(config_yaml)
      return
    end if
    call c_f_pointer(cfg%streams, s_cfg_ptr, [cfg%num_streams])

    call ESMF_VMGetCurrent(vm, rc=rc)
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=rc)

    ! Initialize PIO for standalone TIDE usage
    if (.not. tide_pio_initialized) then
      call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, tide_io_system)
      tide_pio_initialized = .true.
    end if

    ! Allocate array of stream data structures
    tide%num_streams = cfg%num_streams
    allocate(tide%sdat(tide%num_streams))

    ! Initialize each stream separately
    do i = 1, cfg%num_streams
      ! Point each TIDE sdat to our PIO system
      tide%sdat(i)%pio_subsystem => tide_io_system
      tide%sdat(i)%io_type = PIO_IOTYPE_NETCDF
      tide%sdat(i)%io_format = 1

      ! Process files for this stream
      allocate(file_names(s_cfg_ptr(i)%num_files))
      call c_f_pointer(s_cfg_ptr(i)%input_files, input_files_ptr, [s_cfg_ptr(i)%num_files])
      do j = 1, s_cfg_ptr(i)%num_files
        call c_to_f_string(input_files_ptr(j), c_str)
        file_names(j) = trim(c_str)
      end do

      ! Process fields for this stream with CF detection support (Task 13)
      allocate(fld_list_file(s_cfg_ptr(i)%num_fields))
      allocate(fld_list_model(s_cfg_ptr(i)%num_fields))
      call c_f_pointer(s_cfg_ptr(i)%file_vars, file_vars_ptr, [s_cfg_ptr(i)%num_fields])
      call c_f_pointer(s_cfg_ptr(i)%model_vars, model_vars_ptr, [s_cfg_ptr(i)%num_fields])

      ! Initialize CF detection if configured
      call tid_init_cf_detection_for_stream(s_cfg_ptr(i), rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,*) "WARNING: [TIDE] CF detection initialization failed for stream ", i, &
                   ". Falling back to using explicit mapping only."
      end if

      ! Apply CF detection and/or explicit field mapping
      call tide_resolve_field_mappings(s_cfg_ptr(i), file_vars_ptr, model_vars_ptr, &
                                       file_names, fld_list_file, fld_list_model, rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,*) "ERROR: [TIDE] Failed to resolve field mappings for stream ", i, ". Please check your field definitions."
        return
      end if

      ! Get stream-specific parameters
      call c_to_f_string(s_cfg_ptr(i)%mesh_file, mesh_file)
      if (len_trim(mesh_file) == 0 .or. trim(mesh_file) == 'null') mesh_file = 'none'

      call c_to_f_string(s_cfg_ptr(i)%tax_mode, tax_mode)
      call c_to_f_string(s_cfg_ptr(i)%time_interp, time_interp)
      call c_to_f_string(s_cfg_ptr(i)%map_algo, map_algo)

      if (s_cfg_ptr(i)%year_first <= 1 .and. s_cfg_ptr(i)%year_last <= 1) then
          write(*,*) "WARNING: [TIDE] Stream", i, "overriding default year settings with 2000-2010 aligned to 2020"
          s_cfg_ptr(i)%year_first = 2000
          s_cfg_ptr(i)%year_last = 2010
          s_cfg_ptr(i)%year_align = 2020
      endif

      write(*,*) "INFO: [TIDE] Initializing stream", i, "with", s_cfg_ptr(i)%num_fields, "fields"

      ! Initialize this stream using DSHR_STRDATA
      call shr_strdata_init_from_inline(tide%sdat(i), my_task, 6, "TIDE", &
           clock, model_mesh, trim(mesh_file), "null", trim(map_algo), &
           file_names, fld_list_file, fld_list_model, &
           int(s_cfg_ptr(i)%year_first), int(s_cfg_ptr(i)%year_last), int(s_cfg_ptr(i)%year_align), &
           int(s_cfg_ptr(i)%offset), trim(tax_mode), real(s_cfg_ptr(i)%dt_limit, r8), trim(time_interp), &
           rc=rc)

      ! Clean up arrays for next iteration
      deallocate(file_names, fld_list_file, fld_list_model)

      if (rc /= ESMF_SUCCESS) then
        write(*,*) "ERROR: [TIDE] Failed to initialize data stream ", i, &
                   " in DSHR_STRDATA. Check stream configuration and input files."
        return
      end if
    end do

    ! Store year range for clamping (use first stream's settings)
    tide%year_first = s_cfg_ptr(1)%year_first
    tide%year_last = s_cfg_ptr(1)%year_last

    write(*,*) "INFO: [TIDE] Successfully initialized", tide%num_streams, "streams"

    call tide_free_config(c_cfg_ptr)

  end subroutine tide_init

  !> @brief Initializes the TIDE library from an ESMF RC configuration file.
  !> @param tide The TIDE handle to initialize.
  !> @param config_file Path to the ESMF RC configuration file.
  !> @param model_mesh The ESMF Mesh of the model.
  !> @param clock The model's ESMF Clock.
  !> @param rc Return code (ESMF_SUCCESS or ESMF_FAILURE).
  subroutine tide_init_from_esmfconfig(tide, config_file, model_mesh, clock, rc)
    use dshr_strdata_mod, only : shr_strdata_init_from_config
    type(tide_type), intent(inout) :: tide
    character(len=*), intent(in) :: config_file
    type(ESMF_Mesh), intent(in) :: model_mesh
    type(ESMF_Clock), intent(in) :: clock
    integer, intent(out) :: rc

    integer :: my_task, n_tasks, comm
    type(ESMF_VM) :: vm
    type(ESMF_Config) :: cf
    integer :: nstrms
    character(*), parameter :: subName = '(tide_init_from_esmfconfig)'

    rc = ESMF_SUCCESS

    call ESMF_VMGetCurrent(vm, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to get current ESMF Virtual Machine."
      return
    end if
    call ESMF_VMGet(vm, localPet=my_task, petCount=n_tasks, mpiCommunicator=comm, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to get ESMF Virtual Machine parameters."
      return
    end if

    ! Initialize PIO for standalone TIDE usage
    if (.not. tide_pio_initialized) then
      call PIO_Init(my_task, comm, n_tasks, 0, 1, PIO_REARR_BOX, tide_io_system)
      tide_pio_initialized = .true.
    end if

    ! Get number of streams from ESMF config
    cf = ESMF_ConfigCreate(rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to create ESMF Config object."
      return
    end if
    call ESMF_ConfigLoadFile(config=cf, filename=trim(config_file), rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to load ESMF Config file: ", trim(config_file)
      return
    end if

    nstrms = ESMF_ConfigGetLen(config=cf, label='stream_info:', rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to get length of 'stream_info:' from ESMF Config file."
      return
    end if

    call ESMF_ConfigDestroy(cf, rc=rc)

    ! Allocate TIDE structure
    tide%num_streams = nstrms
    if (nstrms > 0) then
      allocate(tide%sdat(tide%num_streams))

      ! Initialize each stream using DSHR_STRDATA
      call shr_strdata_init_from_config(tide%sdat(1), trim(config_file), &
                                       model_mesh, clock, "ACES", 6, rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,*) "ERROR: [TIDE] Failed to initialize stream from ESMF Config file."
        return
      end if
    end if

  end subroutine tide_init_from_esmfconfig

  !> @brief Advances TIDE streams to the current clock time.
  !> @param tide The TIDE handle.
  !> @param clock The current ESMF Clock.
  !> @param rc Return code.
  subroutine tide_advance(tide, clock, rc)
    use shr_cal_mod, only : shr_cal_date2ymd, shr_cal_ymd2date
    type(tide_type), intent(inout) :: tide
    type(ESMF_Clock), intent(in) :: clock
    integer, intent(out) :: rc

    type(ESMF_Time) :: currTime
    integer :: yy, mm, dd, tod, ymd
    integer :: i

    ! Get current time from clock
    call ESMF_ClockGet(clock, currTime=currTime, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to get current time from ESMF Clock."
      return
    end if

    ! Extract YMD and TOD for the interpolation logic
    call ESMF_TimeGet(currTime, yy=yy, mm=mm, dd=dd, s=tod, rc=rc)
    if (rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to extract year, month, day, and seconds from ESMF Time."
      return
    end if

    ! Clamp year to available data range
    if (yy < tide%year_first) yy = tide%year_first
    if (yy > tide%year_last) yy = tide%year_last

    call shr_cal_ymd2date(yy, mm, dd, ymd)

    ! Advance all streams
    do i = 1, tide%num_streams
      call shr_strdata_advance(tide%sdat(i), ymd, tod, 6, "TIDE", rc=rc)
      if (rc /= ESMF_SUCCESS) then
        write(*,*) "ERROR: [TIDE] Failed to advance TIDE stream ", i, " to current time."
        return
      end if
    end do
  end subroutine tide_advance

  !> @brief Retrieves a pointer to the interpolated data for a given field.
  !> @param tide The TIDE handle.
  !> @param field_name Name of the field in the model.
  !> @param ptr 2D pointer to be associated with the field data.
  !> @param rc Return code.
  subroutine tide_get_ptr(tide, field_name, ptr, rc)
    type(tide_type), intent(in) :: tide
    character(len=*), intent(in) :: field_name
    real(r8), pointer :: ptr(:,:)
    integer, intent(out) :: rc

    integer :: i
    integer :: test_rc
    real(r8), pointer :: ptr1d(:)

    rc = -1
    nullify(ptr)

    ! Search for field in all streams — try 2D first (grid-based), then 1D (mesh-based)
    do i = 1, tide%num_streams
      nullify(ptr)
      call shr_strdata_get_stream_pointer(tide%sdat(i), field_name, ptr, test_rc)
      if (test_rc == 0 .and. associated(ptr)) then
        rc = 0
        return
      end if

      ! Try 1D (mesh-based) and reshape to 2D with second dim = 1
      nullify(ptr1d)
      call shr_strdata_get_stream_pointer(tide%sdat(i), field_name, ptr1d, test_rc)
      if (test_rc == 0 .and. associated(ptr1d)) then
        ! Reshape 1D mesh data to 2D (nelems x 1) for compatibility with caller
        ptr(1:size(ptr1d), 1:1) => ptr1d
        rc = 0
        return
      end if
    end do

    ! Field not found in any stream
    write(*,*) "WARNING: [TIDE] Data pointer requested for field '", trim(field_name), &
               "' but it was not found in any initialized TIDE stream."

  end subroutine tide_get_ptr

  !> @brief Destroys TIDE internal objects.
  !> @param tide The TIDE handle.
  !> @param rc Return code.
  subroutine tide_finalize(tide, rc)
    type(tide_type), intent(inout) :: tide
    integer, intent(out) :: rc

    ! Deallocate stream data structures
    if (allocated(tide%sdat)) then
      deallocate(tide%sdat)
    end if

    rc = ESMF_SUCCESS
  end subroutine tide_finalize

  !> @brief Initialize CF detection engine for a single stream (Task 13.1)
  !> @param s_cfg Stream configuration containing CF detection parameters
  !> @param rc Return code (ESMF_SUCCESS or ESMF_FAILURE)
  subroutine tid_init_cf_detection_for_stream(s_cfg, rc)
    type(tide_stream_config_t), intent(in) :: s_cfg
    integer, intent(out) :: rc

    type(cf_detection_config_t) :: cf_config
    character(len=16) :: cf_mode
    integer :: cf_rc

    rc = ESMF_SUCCESS

    ! Extract CF configuration from parsed YAML
    call c_to_f_string(s_cfg%cf_detection_mode, cf_mode)
    cf_config%mode = trim(cf_mode)
    cf_config%cache_enabled = (s_cfg%cf_cache_enabled == 1)
    cf_config%log_level = s_cfg%cf_log_level

    ! Initialize CF detection engine
    call cf_detection_init(cf_config, cf_rc)
    if (cf_rc /= CF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] CF detection engine initialization failed with return code ", cf_rc
      rc = ESMF_FAILURE
      return
    end if

    write(*,*) "INFO: [TIDE] CF detection initialized: mode=", trim(cf_mode), &
               " cache=", cf_config%cache_enabled, " log_level=", cf_config%log_level
  end subroutine tid_init_cf_detection_for_stream

  !> @brief Resolve field mappings using CF detection and explicit mapping (Task 13.1)
  !> @param s_cfg Stream configuration
  !> @param file_vars_ptr C pointer array to file variable names
  !> @param model_vars_ptr C pointer array to model variable names
  !> @param file_names Array of input file names for CF metadata reading
  !> @param fld_list_file Output array of resolved file variable names
  !> @param fld_list_model Output array of resolved model variable names
  !> @param rc Return code (ESMF_SUCCESS or ESMF_FAILURE)
  subroutine tide_resolve_field_mappings(s_cfg, file_vars_ptr, model_vars_ptr, &
                                         file_names, fld_list_file, fld_list_model, rc)
    type(tide_stream_config_t), intent(in) :: s_cfg
    type(c_ptr), pointer, intent(in) :: file_vars_ptr(:)
    type(c_ptr), pointer, intent(in) :: model_vars_ptr(:)
    character(len=cl), intent(in) :: file_names(:)
    character(len=cl), intent(out) :: fld_list_file(:)
    character(len=cl), intent(out) :: fld_list_model(:)
    integer, intent(out) :: rc

    character(len=1024) :: c_str
    character(len=16) :: cf_mode
    character(len=cl) :: model_var, file_var, explicit_file_var
    type(cf_metadata_cache_t) :: cf_cache
    type(cf_variable_metadata_t) :: cf_metadata
    integer :: i, j, cf_rc
    logical :: cf_enabled, has_explicit_mapping
    character(len=cl), allocatable :: explicit_file_vars(:), explicit_model_vars(:)

    rc = ESMF_SUCCESS
    call c_to_f_string(s_cfg%cf_detection_mode, cf_mode)
    cf_enabled = (trim(cf_mode) /= 'disabled')

    ! Extract explicit mappings from YAML for fallback
    allocate(explicit_file_vars(s_cfg%num_fields))
    allocate(explicit_model_vars(s_cfg%num_fields))
    do i = 1, s_cfg%num_fields
      call c_to_f_string(file_vars_ptr(i), c_str)
      explicit_file_vars(i) = trim(c_str)
      call c_to_f_string(model_vars_ptr(i), c_str)
      explicit_model_vars(i) = trim(c_str)
    end do

    ! Read CF metadata from first input file if CF detection enabled
    if (cf_enabled .and. size(file_names) > 0) then
      call cf_read_file_metadata(trim(file_names(1)), tide_io_system, PIO_IOTYPE_NETCDF, cf_cache, cf_rc)
      if (cf_rc /= CF_SUCCESS) then
        write(*,*) "WARNING: [TIDE] CF metadata reading failed, falling back to explicit mapping"
        cf_enabled = .false.
      end if
    end if

    ! Resolve each field mapping
    do i = 1, s_cfg%num_fields
      model_var = trim(explicit_model_vars(i))
      file_var = ''
      has_explicit_mapping = .false.

      ! Try explicit mapping first (takes priority per Requirement 4.2)
      if (len_trim(explicit_file_vars(i)) > 0 .and. trim(explicit_file_vars(i)) /= 'null') then
        file_var = trim(explicit_file_vars(i))
        has_explicit_mapping = .true.
        write(*,*) "INFO: [TIDE] Using explicit mapping: ", trim(model_var), " -> ", trim(file_var)
      end if

      ! Fall back to CF detection if no explicit mapping and CF is enabled
      if (.not. has_explicit_mapping .and. cf_enabled) then
        call cf_match_variable(trim(model_var), cf_cache, file_var, cf_metadata, cf_rc)
        if (cf_rc == CF_SUCCESS) then
          write(*,*) "INFO: [TIDE] CF detection mapped: ", trim(model_var), " -> ", trim(file_var)
        else
          ! Try COARDS as fallback in auto mode or when explicitly in coards mode
          if (trim(cf_mode) == 'auto' .or. trim(cf_mode) == 'coards') then
            call cf_match_variable_coards(trim(model_var), cf_cache, file_var, cf_metadata, cf_rc)
            if (cf_rc == CF_SUCCESS) then
              write(*,*) "INFO: [TIDE] COARDS detection mapped: ", trim(model_var), " -> ", trim(file_var)
            else
              write(*,*) "WARNING: [TIDE] Both CF and COARDS detection failed for: ", trim(model_var)
            end if
          else
            write(*,*) "WARNING: [TIDE] CF detection failed for: ", trim(model_var)
          end if
        end if
      end if

      ! Final validation - ensure we have a mapping
      if (len_trim(file_var) == 0) then
        ! Use fallback: assume model_var == file_var
        file_var = model_var
        write(*,*) "WARNING: [TIDE] No mapping found for ", trim(model_var), ", using identity mapping"
      end if

      fld_list_model(i) = model_var
      fld_list_file(i) = file_var
    end do

    ! Clean up CF metadata cache
    if (cf_enabled) then
      call cf_clear_cache(cf_cache)
    end if

    deallocate(explicit_file_vars, explicit_model_vars)

  end subroutine tide_resolve_field_mappings

end module tide_mod
