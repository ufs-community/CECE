! Thin CCPP cap: scientific calculations remain in the existing CECE kernel.
module cece_native_example_driver
  use machine, only: kind_phys
  use cece_scheme_bindings, only: evaluate_scheme
  implicit none
  private
  public :: cece_native_example_driver_register, cece_native_example_driver_init, cece_native_example_driver_run, cece_native_example_driver_final
contains
  !> \section arg_table_cece_native_example_driver_init Argument Table
  !! \htmlinclude arg_table_cece_native_example_driver_init.html
  subroutine cece_native_example_driver_init(errmsg, errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    errmsg = ''
    errflg = 0
  end subroutine
  !> \section arg_table_cece_native_example_driver_final Argument Table
  !! \htmlinclude arg_table_cece_native_example_driver_final.html
  subroutine cece_native_example_driver_final(errmsg, errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    ! Kokkos belongs to the application, not individual scheme finalizers.
    errmsg = ''
    errflg = 0
  end subroutine
  !> @brief Register this fixed emission-flux interface (no prognostic constituents).
  !> \section arg_table_cece_native_example_driver_register Argument Table
  !! \htmlinclude arg_table_cece_native_example_driver_register.html
  subroutine cece_native_example_driver_register(errmsg,errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    errmsg=''
    errflg=0
  end subroutine
  !> \section arg_table_cece_native_example_driver_run Argument Table
  !! \htmlinclude arg_table_cece_native_example_driver_run.html
  subroutine cece_native_example_driver_run(ncol, time, dt, nlev, base_nox, multiplier, emissions, errmsg, errflg)
    integer, intent(in) :: ncol, nlev
    real(kind_phys), intent(in) :: time, dt, multiplier, base_nox(:,:)
    real(kind_phys), intent(inout) :: emissions(:,:)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    character(len=128) :: options
    real(kind_phys) :: inputs(ncol*nlev,1), outputs(ncol*nlev,1)
    if (size(base_nox,1)/=ncol .or. size(base_nox,2)/=nlev .or. &
        size(emissions,1)/=ncol .or. size(emissions,2)/=nlev) then
      errmsg='CECE native example shape mismatch'
      errflg=1
      return
    end if
    write(options,'(a,es24.16)') 'multiplier: ',multiplier
    inputs(:,1)=reshape(base_nox,[ncol*nlev])
    outputs=0
    call evaluate_scheme('native_example',trim(options),ncol,nlev,nlev, &
        ['base_anthropogenic_nox'],inputs,['nox'],outputs,time,dt,errmsg,errflg)
    if (errflg==0) emissions=reshape(outputs(:,1),[ncol,nlev])
  end subroutine
end module
