module dshr_methods_mod

  ! Share methods for data model functionality

  use ESMF         , only : ESMF_State, ESMF_Field, ESMF_StateGet, ESMF_FieldBundle
  use ESMF         , only : ESMF_LogWrite, ESMF_SUCCESS, ESMF_FAILURE
  use ESMF         , only : ESMF_StateRemove, ESMF_StateGet, ESMF_RouteHandle
  use ESMF         , only : ESMF_Region_Flag, ESMF_FieldStatus_Flag, ESMF_LOGMSG_INFO
  use ESMF         , only : ESMF_MAXSTR, ESMF_LOGMSG_ERROR, ESMF_LOGERR_PASSTHRU
  use ESMF         , only : ESMF_FieldBundleGet, ESMF_FieldBundleAdd, ESMF_FieldGet
  use ESMF         , only : ESMF_REGION_TOTAL, ESMF_END_ABORT, ESMF_ITEMORDER_ADDORDER
  use ESMF         , only : ESMF_LogFoundError, ESMF_FieldRegrid, ESMF_Finalize, ESMF_FIELDSTATUS_COMPLETE
  use ESMF         , only : ESMF_TERMORDER_SRCSEQ, operator(/=)
  use ESMF         , only : ESMF_TraceRegionEnter, ESMF_TraceRegionExit
  use shr_kind_mod , only : r8=>shr_kind_r8, cs=>shr_kind_cs, cl=>shr_kind_cl
  ! Remove shr_cal_mod dependency since we'll implement locally

  implicit none
  public

  public :: dshr_state_getfldptr
  public :: dshr_state_diagnose
  public :: dshr_fldbun_getFldPtr
  public :: dshr_fldbun_regrid
  public :: dshr_fldbun_getFieldN
  public :: dshr_fldbun_getNameN
  public :: dshr_fldbun_fldchk
  public :: dshr_fldbun_diagnose
  public :: dshr_field_getfldptr
  public :: chkerr
  public :: memcheck
  public :: dshr_cal_aligndow  ! Align dates to day of week

  private :: get_day_of_week  ! Local implementation
  private :: advance_date     ! Local implementation

  character(len=1024) :: msgString
  integer, parameter  :: memdebug_level=1
  character(*), parameter :: u_FILE_u = &
       __FILE__

!===============================================================================
contains
!===============================================================================

  subroutine dshr_state_getfldptr(State, fldname, fldptr1, fldptr2, allowNullReturn, rc)

    ! ----------------------------------------------
    ! Get pointer to a state field
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_State) ,          intent(in)              :: State
    character(len=*) ,          intent(in)              :: fldname
    real(R8)         , pointer, intent(inout), optional :: fldptr1(:)
    real(R8)         , pointer, intent(inout), optional :: fldptr2(:,:)
    logical          ,          intent(in),optional     :: allowNullReturn
    integer          ,          intent(out)             :: rc

    ! local variables
    type(ESMF_Field)            :: lfield
    integer                     :: itemCount
    character(len=*), parameter :: subname='(dshr_state_getfldptr)'
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    if (present(allowNullReturn)) then
      call ESMF_StateGet(State, itemSearch=trim(fldname), itemCount=itemCount, rc=rc)
      if (chkerr(rc,__LINE__,u_FILE_u)) return

      ! check field is in the state or not?
      if (itemCount >= 1) then
        call ESMF_StateGet(State, itemName=trim(fldname), field=lfield, rc=rc)
        if (chkerr(rc,__LINE__,u_FILE_u)) return

        call dshr_field_getfldptr(lfield, fldptr1=fldptr1, fldptr2=fldptr2, rc=rc)
        if (chkerr(rc,__LINE__,u_FILE_u)) return
      else
        ! the call to just returns if it cannot find the field
        call ESMF_LogWrite(trim(subname)//" Could not find the field: "//trim(fldname)//" just returning", ESMF_LOGMSG_INFO)
      end if
    else
      call ESMF_StateGet(State, itemName=trim(fldname), field=lfield, rc=rc)
      if (chkerr(rc,__LINE__,u_FILE_u)) return

      call dshr_field_getfldptr(lfield, fldptr1=fldptr1, fldptr2=fldptr2, rc=rc)
      if (chkerr(rc,__LINE__,u_FILE_u)) return
    end if

  end subroutine dshr_state_getfldptr

  !===============================================================================
  subroutine dshr_state_diagnose(State, flds_scalar_name, string, rc)

    ! ----------------------------------------------
    ! Diagnose status of State
    ! ----------------------------------------------

    type(ESMF_State), intent(in)  :: state
    character(len=*), intent(in)  :: flds_scalar_name
    character(len=*), intent(in)  :: string
    integer         , intent(out) :: rc

    ! local variables
    integer                         :: n
    type(ESMf_Field)                :: lfield
    integer                         :: fieldCount, lrank
    character(ESMF_MAXSTR) ,pointer :: lfieldnamelist(:)
    real(r8), pointer               :: dataPtr1d(:)
    real(r8), pointer               :: dataPtr2d(:,:)
    character(len=*),parameter      :: subname='(dshr_state_diagnose)'
    ! ----------------------------------------------

    call ESMF_StateGet(state, itemCount=fieldCount, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    allocate(lfieldnamelist(fieldCount))
    call ESMF_StateGet(state, itemNameList=lfieldnamelist, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

    do n = 1, fieldCount
       call ESMF_StateGet(state, itemName=lfieldnamelist(n), field=lfield, rc=rc)
       if (chkerr(rc,__LINE__,u_FILE_u)) return

       if (trim(lfieldnamelist(n)) /= trim(flds_scalar_name)) then

          call dshr_field_getfldptr(lfield, fldptr1=dataPtr1d, fldptr2=dataPtr2d, rank=lrank, rc=rc)
          if (chkerr(rc,__LINE__,u_FILE_u)) return

          if (lrank == 0) then
             ! no local data
          elseif (lrank == 1) then
             if (size(dataPtr1d) > 0) then
                write(msgString,'(A,3g14.7,i8)') trim(string)//': '//trim(lfieldnamelist(n)), &
                     minval(dataPtr1d), maxval(dataPtr1d), sum(dataPtr1d), size(dataPtr1d)
             else
                write(msgString,'(A,a)') trim(string)//': '//trim(lfieldnamelist(n))," no data"
             endif
          elseif (lrank == 2) then
             if (size(dataPtr2d) > 0) then
                write(msgString,'(A,3g14.7,i8)') trim(string)//': '//trim(lfieldnamelist(n)), &
                     minval(dataPtr2d), maxval(dataPtr2d), sum(dataPtr2d), size(dataPtr2d)
             else
                write(msgString,'(A,a)') trim(string)//': '//trim(lfieldnamelist(n))," no data"
             endif
          else
             call ESMF_LogWrite(trim(subname)//": ERROR rank not supported ", ESMF_LOGMSG_ERROR)
             rc = ESMF_FAILURE
             return
          endif
          call ESMF_LogWrite(trim(msgString), ESMF_LOGMSG_INFO)
       end if
    enddo

    deallocate(lfieldnamelist)

  end subroutine dshr_state_diagnose

  !===============================================================================
  subroutine dshr_fldbun_GetFldPtr(FB, fldname, fldptr1, fldptr2, rank, field, rc)

    ! ----------------------------------------------
    ! Get pointer to a field bundle field
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_FieldBundle) , intent(in)              :: FB
    character(len=*)       , intent(in)              :: fldname
    real(R8), pointer      , intent(inout), optional :: fldptr1(:)
    real(R8), pointer      , intent(inout), optional :: fldptr2(:,:)
    integer                , intent(out),   optional :: rank
    type(ESMF_Field)       , intent(out),   optional :: field
    integer                , intent(out)             :: rc

    ! local variables
    integer           :: lrank
    type(ESMF_Field)  :: lfield
    integer           :: ungriddedUBound(1)
    character(len=*), parameter :: subname='(dshr_fldbun_GetFldPtr)'
    real(R8), pointer :: temp_ptr(:)
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    if (.not. dshr_fldbun_FldChk(FB, trim(fldname), rc=rc)) then
       call ESMF_LogWrite(trim(subname)//": ERROR field "//trim(fldname)//" not in FB ", ESMF_LOGMSG_ERROR)
      rc = ESMF_FAILURE
      return
    endif
    call ESMF_FieldBundleGet(FB, fieldName=trim(fldname), field=lfield, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    call ESMF_FieldGet(lfield, ungriddedUBound=ungriddedUBound, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    if (ungriddedUBound(1) > 0) then
       if (.not.present(fldptr2)) then
          call ESMF_LogWrite(trim(subname)//": ERROR missing rank=2 array ", &
               ESMF_LOGMSG_ERROR, line=__LINE__, file=u_FILE_u)
          rc = ESMF_FAILURE
          return
       endif
       call ESMF_FieldGet(lfield, farrayptr=fldptr2, rc=rc)
       if (chkerr(rc,__LINE__,u_FILE_u)) return
       lrank = 2
    else
       if (present(fldptr1)) then
          call ESMF_FieldGet(lfield, farrayptr=fldptr1, rc=rc)
          if (chkerr(rc,__LINE__,u_FILE_u)) return
          lrank = 1
       elseif (present(fldptr2)) then
          ! Special handling: retrieve rank-1 field into rank-2 pointer (N,1)
          call ESMF_FieldGet(lfield, farrayptr=temp_ptr, rc=rc)
          if (chkerr(rc,__LINE__,u_FILE_u)) return
          fldptr2(1:size(temp_ptr), 1:1) => temp_ptr
          lrank = 2
       else
          call ESMF_LogWrite(trim(subname)//": ERROR missing rank=1 array ", &
               ESMF_LOGMSG_ERROR, line=__LINE__, file=u_FILE_u)
          print *, "DEBUG: dshr_fldbun_GetFldPtr: ungriddedUBound(1)=", ungriddedUBound(1)
          print *, "DEBUG: present(fldptr1)=", present(fldptr1), " present(fldptr2)=", present(fldptr2)
          rc = ESMF_FAILURE
          return
       end if
    end if
    if (present(rank)) rank = lrank
    if (present(field)) field = lfield

  end subroutine dshr_fldbun_GetFldPtr

  !===============================================================================
  subroutine dshr_fldbun_regrid(FBsrc, FBdst, RH, zeroregion, rc)

    ! ----------------------------------------------
    ! Assumes that FBin and FBout contain fields with the same name
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_FieldBundle), intent(inout)        :: FBsrc
    type(ESMF_FieldBundle), intent(inout)        :: FBdst
    type(ESMF_RouteHandle), intent(inout)        :: RH
    type(ESMF_Region_Flag), intent(in), optional :: zeroregion
    integer               , intent(out)          :: rc

    ! local
    integer                    :: n
    type(ESMF_Region_Flag)     :: localzr
    type(ESMF_Field)           :: field_src
    type(ESMF_Field)           :: field_dst
    integer                    :: fieldcount_src
    integer                    :: fieldcount_dst
    character(ESMF_MAXSTR), allocatable :: lfieldNameList_src(:)
    character(ESMF_MAXSTR), allocatable :: lfieldNameList_dst(:)
    character(len=*),parameter :: subname='(dshr_fldbun_FieldRegrid)'
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    call ESMF_TraceRegionEnter(subname)

    localzr = ESMF_REGION_TOTAL
    if (present(zeroregion)) then
       localzr = zeroregion
    endif

    call ESMF_FieldBundleGet(FBsrc, fieldCount=fieldCount_src, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    allocate(lfieldNameList_src(fieldCount_src))
    call ESMF_FieldBundleGet(FBsrc, fieldNameList=lfieldNameList_src, itemorderflag=ESMF_ITEMORDER_ADDORDER, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

    call ESMF_FieldBundleGet(FBdst, fieldCount=fieldCount_dst, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    allocate(lfieldNameList_dst(fieldCount_dst))
    call ESMF_FieldBundleGet(FBdst, fieldNameList=lfieldNameList_dst, itemorderflag=ESMF_ITEMORDER_ADDORDER, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

    ! check that input and output field bundles have identical number of fields
    if (fieldcount_src /= fieldcount_dst) then
       call ESMF_LogWrite(trim(subname)//": ERROR fieldcount_src and field_count_dst are not the same")
       rc = ESMF_FAILURE
       if (ESMF_LogFoundError(rcToCheck=rc, msg=ESMF_LOGERR_PASSTHRU, line=__LINE__, file=u_FILE_u)) then
          call ESMF_Finalize(endflag=ESMF_END_ABORT)
       end if
    end if

    do n = 1,fieldCount_src
       call ESMF_FieldBundleGet(FBsrc, fieldName=trim(lfieldnamelist_src(n)), field=field_src, rc=rc)
       if (chkerr(rc,__LINE__,u_FILE_u)) return

       call ESMF_FieldBundleGet(FBdst, fieldName=trim(lfieldnamelist_dst(n)), field=field_dst, rc=rc)
       if (chkerr(rc,__LINE__,u_FILE_u)) return

       call ESMF_FieldRegrid(field_src, field_dst, routehandle=RH, &
            termorderflag=ESMF_TERMORDER_SRCSEQ, checkflag=.false., zeroregion=localzr, rc=rc)
       if (chkerr(rc,__LINE__,u_FILE_u)) return
    end do

    deallocate(lfieldnamelist_src)
    deallocate(lfieldnamelist_dst)

    call ESMF_TraceRegionExit(subname)

  end subroutine dshr_fldbun_regrid

  !===============================================================================
  subroutine dshr_fldbun_getFieldN(FB, fieldnum, field, rc)

    ! ----------------------------------------------
    ! Get field with number fieldnum in input field bundle FB
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_FieldBundle), intent(in)    :: FB
    integer               , intent(in)    :: fieldnum
    type(ESMF_Field)      , intent(inout) :: field
    integer               , intent(out)   :: rc

    ! local variables
    character(len=ESMF_MAXSTR) :: name
    character(len=*),parameter :: subname='(dshr_fldbun_getFieldN)'
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    call dshr_fldbun_getNameN(FB, fieldnum, name, rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

    call ESMF_FieldBundleGet(FB, fieldName=name, field=field, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

  end subroutine dshr_fldbun_getFieldN

  !===============================================================================
  subroutine dshr_fldbun_getNameN(FB, fieldnum, fieldname, rc)

    ! ----------------------------------------------
    ! Get name of field number fieldnum in input field bundle FB
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_FieldBundle), intent(in)    :: FB
    integer               , intent(in)    :: fieldnum
    character(len=*)      , intent(out)   :: fieldname
    integer               , intent(out)   :: rc

    ! local variables
    integer                         :: fieldCount
    character(ESMF_MAXSTR) ,pointer :: lfieldnamelist(:)
    character(len=*),parameter      :: subname='(dshr_fldbun_getNameN)'
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    fieldname = ' '
    call ESMF_FieldBundleGet(FB, fieldCount=fieldCount, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    if (fieldnum > fieldCount) then
      call ESMF_LogWrite(trim(subname)//": ERROR fieldnum > fieldCount ", ESMF_LOGMSG_ERROR)
      rc = ESMF_FAILURE
      return
    endif

    allocate(lfieldnamelist(fieldCount))
    call ESMF_FieldBundleGet(FB, fieldNameList=lfieldnamelist, itemorderflag=ESMF_ITEMORDER_ADDORDER, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

    fieldname = lfieldnamelist(fieldnum)
    deallocate(lfieldnamelist)

  end subroutine dshr_fldbun_getNameN

  !===============================================================================
  logical function dshr_fldbun_FldChk(FB, fldname, rc)

    ! ----------------------------------------------
    ! Determine if field with fldname is in input field bundle
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_FieldBundle), intent(in)  :: FB
    character(len=*)      , intent(in)  :: fldname
    integer               , intent(out) :: rc

    ! local variables
    logical                     :: isPresent
    character(len=*), parameter :: subname='(dshr_fldbun_FldChk)'
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    ! If field bundle is created determine if fldname is present in field bundle
    dshr_fldbun_FldChk = .false.

    call ESMF_FieldBundleGet(FB, fieldName=trim(fldname), isPresent=isPresent, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) then
       call ESMF_LogWrite(trim(subname)//" Error checking field: "//trim(fldname), ESMF_LOGMSG_ERROR)
       rc = ESMF_FAILURE
       return
    endif

    if (isPresent) then
       dshr_fldbun_FldChk = .true.
    endif

  end function dshr_fldbun_FldChk

  !===============================================================================
  subroutine dshr_fldbun_Field_diagnose(FB, fieldname, string, rc)

    ! ----------------------------------------------
    ! Diagnose status of State
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_FieldBundle), intent(inout)  :: FB
    character(len=*), intent(in)           :: fieldname
    character(len=*), intent(in), optional :: string
    integer         , intent(out)          :: rc

    ! local variables
    integer           :: lrank
    character(len=CS) :: lstring
    real(R8), pointer :: dataPtr1d(:)
    real(R8), pointer :: dataPtr2d(:,:)
    character(len=*),parameter      :: subname='(dshr_fldbun_FieldDiagnose)'
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    lstring = ''
    if (present(string)) lstring = trim(string)

    call dshr_fldbun_GetFldPtr(FB, fieldname, dataPtr1d, dataPtr2d, lrank, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

    if (lrank == 0) then
       ! no local data
    elseif (lrank == 1) then
       if (size(dataPtr1d) > 0) then
          write(msgString,'(A,3g14.7,i8)') trim(subname)//' '//trim(lstring)//': '//trim(fieldname), &
               minval(dataPtr1d), maxval(dataPtr1d), sum(dataPtr1d), size(dataPtr1d)
       else
          write(msgString,'(A,a)') trim(subname)//' '//trim(lstring)//': '//trim(fieldname)," no data"
       endif
    elseif (lrank == 2) then
       if (size(dataPtr2d) > 0) then
          write(msgString,'(A,3g14.7,i8)') trim(subname)//' '//trim(lstring)//': '//trim(fieldname), &
               minval(dataPtr2d), maxval(dataPtr2d), sum(dataPtr2d), size(dataPtr2d)
       else
          write(msgString,'(A,a)') trim(subname)//' '//trim(lstring)//': '//trim(fieldname)," no data"
       endif
    else
       call ESMF_LogWrite(trim(subname)//": ERROR rank not supported ", ESMF_LOGMSG_ERROR)
       rc = ESMF_FAILURE
       return
    endif
    call ESMF_LogWrite(trim(msgString), ESMF_LOGMSG_INFO)

  end subroutine dshr_fldbun_Field_diagnose


 !===============================================================================
  subroutine dshr_fldbun_diagnose(FB, string, rc)

    ! ----------------------------------------------
    ! Diagnose status of FB
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_FieldBundle) , intent(inout)        :: FB
    character(len=*)       , intent(in), optional :: string
    integer                , intent(out)          :: rc

    ! local variables
    integer                         :: n
    integer                         :: fieldCount, lrank
    character(ESMF_MAXSTR), pointer :: lfieldnamelist(:)
    character(len=CL)               :: lstring
    real(R8), pointer               :: dataPtr1d(:)
    real(R8), pointer               :: dataPtr2d(:,:)
    character(len=*), parameter     :: subname='(dshr_fldbun_diagnose)'
    ! ----------------------------------------------

    rc = ESMF_SUCCESS

    lstring = ''
    if (present(string)) lstring = trim(string) // ' '

    ! Determine number of fields in field bundle and allocate memory for lfieldnamelist
    call ESMF_FieldBundleGet(FB, fieldCount=fieldCount, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    allocate(lfieldnamelist(fieldCount))

    ! Get the fields in the field bundle
    call ESMF_FieldBundleGet(FB, fieldNameList=lfieldnamelist, itemorderflag=ESMF_ITEMORDER_ADDORDER, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return

    ! For each field in the bundle, get its memory location and print out the field
    do n = 1, fieldCount
       call dshr_fldbun_GetFldPtr(FB, lfieldnamelist(n), fldptr1=dataPtr1d, fldptr2=dataPtr2d, rank=lrank, rc=rc)
       if (chkerr(rc,__LINE__,u_FILE_u)) return

       if (lrank == 0) then
          ! no local data

       elseif (lrank == 1) then
          if (size(dataPtr1d) > 0) then
             write(msgString,'(A,3g14.7,i8)') trim(subname)//' '//trim(lstring)//': '//trim(lfieldnamelist(n))//' ', &
                  minval(dataPtr1d), maxval(dataPtr1d), sum(dataPtr1d), size(dataPtr1d)
          else
             write(msgString,'(A,a)') trim(subname)//' '//trim(lstring)//': '//trim(lfieldnamelist(n)), " no data"
          endif

       elseif (lrank == 2) then
          if (size(dataPtr2d) > 0) then
             write(msgString,'(A,3g14.7,i8)') trim(subname)//' '//trim(lstring)//': '//trim(lfieldnamelist(n))//' ', &
                  minval(dataPtr2d), maxval(dataPtr2d), sum(dataPtr2d), size(dataPtr2d)
          else
             write(msgString,'(A,a)') trim(subname)//' '//trim(lstring)//': '//trim(lfieldnamelist(n)), &
                  " no data"
          endif

       else
          call ESMF_LogWrite(trim(subname)//": ERROR rank not supported ", ESMF_LOGMSG_ERROR)
          rc = ESMF_FAILURE
          return
       endif
       call ESMF_LogWrite(trim(msgString), ESMF_LOGMSG_INFO)
    enddo

    ! Deallocate memory
    deallocate(lfieldnamelist)

    call ESMF_LogWrite(trim(subname)//": done", ESMF_LOGMSG_INFO)

  end subroutine dshr_fldbun_diagnose

  !===============================================================================
  subroutine dshr_field_getfldptr(field, fldptr1, fldptr2, rank, abort, rc)

    ! ----------------------------------------------
    ! for a field, determine rank and return fldptr1 or fldptr2
    ! abort is true by default and will abort if fldptr is not yet allocated in field
    ! rank returns 0, 1, or 2.  0 means fldptr not allocated and abort=false
    ! ----------------------------------------------

    ! input/output variables
    type(ESMF_Field)  , intent(in)              :: field
    real(r8), pointer , intent(inout), optional :: fldptr1(:)
    real(r8), pointer , intent(inout), optional :: fldptr2(:,:)
    integer           , intent(out)  , optional :: rank
    logical           , intent(in)   , optional :: abort
    integer           , intent(out)             :: rc

    ! local variables
    type(ESMF_FieldStatus_Flag) :: status
    integer                     :: ungriddedUBound(1)
    integer                     :: lrank
    logical                     :: labort
    character(len=CS)           :: name
    character(len=*), parameter :: subname='(field_getfldptr)'
    ! ----------------------------------------------
    rc = ESMF_SUCCESS
    lrank = 0
    labort = .true.
    if (present(abort)) then
       labort = abort
    endif

    call ESMF_FieldGet(field, status=status, rc=rc)
    if (chkerr(rc,__LINE__,u_FILE_u)) return
    if (status /= ESMF_FIELDSTATUS_COMPLETE) then
       if (labort) then
          call ESMF_FieldGet(field, name=name, rc=rc)
          if (chkerr(rc,__LINE__,u_FILE_u)) return
          call ESMF_LogWrite(trim(subname)//": field "//trim(name)//" has no data not allocated ", ESMF_LOGMSG_ERROR, rc=rc)
          rc = ESMF_FAILURE
          return
       else
          call ESMF_LogWrite(trim(subname)//": WARNING data not allocated ", ESMF_LOGMSG_INFO, rc=rc)
       endif
    else
        call ESMF_FieldGet(field, ungriddedUBound=ungriddedUBound, rc=rc)
        if (chkerr(rc,__LINE__,u_FILE_u)) return
        if (ungriddedUBound(1) > 0) then
           if (.not.present(fldptr2)) then
              call ESMF_LogWrite(trim(subname)//": ERROR missing rank=2 array for "//trim(name), &
                   ESMF_LOGMSG_ERROR, line=__LINE__, file=u_FILE_u)
              rc = ESMF_FAILURE
              return
           endif
           call ESMF_FieldGet(field, farrayptr=fldptr2, rc=rc)
           if (chkerr(rc,__LINE__,u_FILE_u)) return
           lrank = 2
        else
           if (.not.present(fldptr1)) then
              call ESMF_LogWrite(trim(subname)//": ERROR missing rank=1 array for "//trim(name), &
                   ESMF_LOGMSG_ERROR, line=__LINE__, file=u_FILE_u)
              rc = ESMF_FAILURE
              return
           endif
           call ESMF_FieldGet(field, farrayptr=fldptr1, rc=rc)
           if (chkerr(rc,__LINE__,u_FILE_u)) return
           lrank = 1
        end if
    endif  ! status
    if (present(rank)) rank = lrank

  end subroutine dshr_field_getfldptr

  !===============================================================================
  subroutine memcheck(string, level, maintask)

    ! input/output variables
    character(len=*) , intent(in) :: string
    integer          , intent(in) :: level
    logical          , intent(in) :: maintask

    ! local variables
#ifdef CESMCOUPLED
    integer :: ierr
    integer, external :: GPTLprint_memusage
#endif
    !-----------------------------------------------------------------------

#ifdef CESMCOUPLED
    if ((maintask .and. memdebug_level > level) .or. memdebug_level > level+1) then
       ierr = GPTLprint_memusage(string)
    endif
#endif

  end subroutine memcheck

  !===============================================================================
  logical function chkerr(rc, line, file)
    integer, intent(in) :: rc
    integer, intent(in) :: line
    character(len=*), intent(in) :: file
    integer :: lrc
    !-----------------------------------------------------------------------
    chkerr = .false.
    lrc = rc
    if (ESMF_LogFoundError(rcToCheck=lrc, msg=ESMF_LOGERR_PASSTHRU, line=line, file=file)) then
       chkerr = .true.
    endif
  end function chkerr

  !===============================================================================
  subroutine dshr_cal_aligndow(year, month, day, target_dow, aligned_ymd)
    ! Aligns a date to the closest matching day of week
    ! target_dow: 0=Sunday, 1=Monday, ..., 6=Saturday
    ! For weekday targets (1-5), aligns to closest weekday
    ! For weekend targets (0,6), aligns to closest weekend day
    integer, intent(in)  :: year, month, day   ! Input date
    integer, intent(in)  :: target_dow         ! Target day of week
    integer, intent(out) :: aligned_ymd        ! Aligned date in YYYYMMDD format

    integer :: current_dow, delta_days
    integer :: work_year, work_month, work_day
    logical :: target_is_weekend, current_is_weekend

    work_year = year
    work_month = month
    work_day = day

    ! Get current day of week using local implementation
    current_dow = get_day_of_week(work_year, work_month, work_day)

    ! Determine if target and current days are weekends
    target_is_weekend = (target_dow == 0 .or. target_dow == 6)
    current_is_weekend = (current_dow == 0 .or. current_dow == 6)

    if (target_is_weekend) then
      if (.not. current_is_weekend) then
        ! Current is weekday, find closest weekend day
        if (target_dow == 0) then  ! Target is Sunday
          if (current_dow < 3) then
            delta_days = -current_dow  ! Go back to previous Sunday
          else
            delta_days = 6 - current_dow  ! Go forward to next Saturday
          endif
        else  ! Target is Saturday
          if (current_dow < 4) then
            delta_days = -(current_dow + 1)  ! Go back to previous Saturday
          else
            delta_days = 6 - current_dow  ! Go forward to next Saturday
          endif
        endif
      else
        ! Current is already weekend, use standard alignment
        delta_days = mod(target_dow - current_dow, 7)
        if (delta_days > 3) delta_days = delta_days - 7  ! Use shorter path
      endif
    else
      if (current_is_weekend) then
        ! Current is weekend, move to closest weekday
        if (current_dow == 0) then  ! Sunday
          delta_days = 1  ! Move to Monday
        else  ! Saturday
          delta_days = -1  ! Move to Friday
        endif
      else
        ! Both are weekdays, find closest match
        delta_days = target_dow - current_dow
        if (abs(delta_days) > 2) then
          ! Take shorter path around the weekend
          if (delta_days > 0) then
            delta_days = delta_days - 5
          else
            delta_days = delta_days + 5
          endif
        endif
      endif
    endif

    ! Apply the calculated shift using local implementation
    call advance_date(delta_days, work_year, work_month, work_day)
    aligned_ymd = work_year*10000 + work_month*100 + work_day

  end subroutine dshr_cal_aligndow

  !===============================================================================
  function get_day_of_week(year, month, day) result(dow)
    ! Returns day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
    ! Using Zeller's congruence algorithm
    integer, intent(in) :: year, month, day
    integer :: dow, m, y, k, d

    if (month <= 2) then
      m = month + 12
      y = year - 1
    else
      m = month
      y = year
    endif

    k = mod(y, 100)
    y = y / 100

    d = day + ((13*(m+1))/5) + k + (k/4) + (y/4) - 2*y
    dow = mod(d+77, 7)  ! Adjust to make Sunday = 0

  end function get_day_of_week

  !===============================================================================
  subroutine advance_date(delta_days, year, month, day)
    ! Advance or retreat a date by specified number of days
    integer, intent(in)    :: delta_days
    integer, intent(inout) :: year, month, day

    integer :: days_in_month(12) = (/31,28,31,30,31,30,31,31,30,31,30,31/)
    integer :: remaining_days, dir

    ! Handle leap years
    if (mod(year,4) == 0 .and. (mod(year,100) /= 0 .or. mod(year,400) == 0)) then
      days_in_month(2) = 29
    endif

    remaining_days = delta_days
    dir = sign(1, delta_days)

    do while (abs(remaining_days) > 0)
      if (dir > 0) then
        ! Moving forward in time
        if (day + 1 > days_in_month(month)) then
          day = 1
          if (month == 12) then
            month = 1
            year = year + 1
            ! Update February for new year
            days_in_month(2) = 28
            if (mod(year,4) == 0 .and. (mod(year,100) /= 0 .or. mod(year,400) == 0)) then
              days_in_month(2) = 29
            endif
          else
            month = month + 1
          endif
        else
          day = day + 1
        endif
      else
        ! Moving backward in time
        if (day - 1 < 1) then
          if (month == 1) then
            month = 12
            year = year - 1
            ! Update February for new year
            days_in_month(2) = 28
            if (mod(year,4) == 0 .and. (mod(year,100) /= 0 .or. mod(year,400) == 0)) then
              days_in_month(2) = 29
            endif
          else
            month = month - 1
          endif
          day = days_in_month(month)
        else
          day = day - 1
        endif
      endif
      remaining_days = remaining_days - dir
    end do

  end subroutine advance_date

end module dshr_methods_mod
