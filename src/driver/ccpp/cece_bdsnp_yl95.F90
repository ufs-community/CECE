! Thin CCPP cap: scientific calculations remain in the existing CECE kernel.
module cece_bdsnp_yl95_driver
  use machine, only: kind_phys
  use cece_scheme_bindings, only: evaluate_scheme
  implicit none
  private
  public :: cece_bdsnp_yl95_driver_register, cece_bdsnp_yl95_driver_init, cece_bdsnp_yl95_driver_run, cece_bdsnp_yl95_driver_final
contains
  !> \section arg_table_cece_bdsnp_yl95_driver_init Argument Table
  !! \htmlinclude arg_table_cece_bdsnp_yl95_driver_init.html
  subroutine cece_bdsnp_yl95_driver_init(errmsg, errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    errmsg = ''
    errflg = 0
  end subroutine
  !> \section arg_table_cece_bdsnp_yl95_driver_final Argument Table
  !! \htmlinclude arg_table_cece_bdsnp_yl95_driver_final.html
  subroutine cece_bdsnp_yl95_driver_final(errmsg, errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    ! Kokkos belongs to the application, not individual scheme finalizers.
    errmsg = ''
    errflg = 0
  end subroutine
  !> rief Register this fixed emission-flux interface (no prognostic constituents).
  !> \section arg_table_cece_bdsnp_yl95_driver_register Argument Table
  !! \htmlinclude arg_table_cece_bdsnp_yl95_driver_register.html
  subroutine cece_bdsnp_yl95_driver_register(errmsg,errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    errmsg=''
    errflg=0
  end subroutine
  !> \section arg_table_cece_bdsnp_yl95_driver_run Argument Table
  !! \htmlinclude arg_table_cece_bdsnp_yl95_driver_run.html
  subroutine cece_bdsnp_yl95_driver_run(ncol,time,dt,soil_temperature,soil_moisture,emissions,errmsg,errflg)
    integer, intent(in) :: ncol
    real(kind_phys), intent(in) :: time,dt,soil_temperature(:),soil_moisture(:)
    real(kind_phys), intent(inout) :: emissions(:)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    real(kind_phys) :: inputs(ncol,2),outputs(ncol,1)
    if (size(soil_temperature)/=ncol .or. size(soil_moisture)/=ncol .or. size(emissions)/=ncol) then
      errmsg='CECE BDSNP YL95 shape mismatch'
      errflg=1
      return
    end if
    inputs(:,1)=soil_temperature
    inputs(:,2)=soil_moisture
    outputs=0
    call evaluate_scheme('bdsnp','soil_no_method: yl95',ncol,1,1, &
        [character(len=32)::'soil_temperature','soil_moisture'],inputs, &
        ['soil_nox_emissions'],outputs,time,dt,errmsg,errflg)
    if (errflg==0) emissions=outputs(:,1)
  end subroutine
end module
