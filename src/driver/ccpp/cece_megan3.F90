! Thin CCPP cap: scientific calculations remain in the existing CECE kernel.
module cece_megan3_driver
  use iso_c_binding, only: c_double
  use machine, only: kind_phys
  use cece_scheme_bindings, only: evaluate_scheme
  implicit none
  private
  public :: cece_megan3_driver_register, cece_megan3_driver_init, cece_megan3_driver_run, cece_megan3_driver_final
contains
  !> \section arg_table_cece_megan3_driver_init Argument Table
  !! \htmlinclude arg_table_cece_megan3_driver_init.html
  subroutine cece_megan3_driver_init(errmsg, errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    errmsg = ''
    errflg = 0
  end subroutine
  !> \section arg_table_cece_megan3_driver_final Argument Table
  !! \htmlinclude arg_table_cece_megan3_driver_final.html
  subroutine cece_megan3_driver_final(errmsg, errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    ! Kokkos belongs to the application, not individual scheme finalizers.
    errmsg = ''
    errflg = 0
  end subroutine
  !> @brief Register this fixed emission-flux interface (no prognostic constituents).
  !> \section arg_table_cece_megan3_driver_register Argument Table
  !! \htmlinclude arg_table_cece_megan3_driver_register.html
  subroutine cece_megan3_driver_register(errmsg,errflg)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    errmsg=''
    errflg=0
  end subroutine
  !> \section arg_table_cece_megan3_driver_run Argument Table
  !! \htmlinclude arg_table_cece_megan3_driver_run.html
  subroutine cece_megan3_driver_run(ncol,time,dt,nspecies,temperature,lai,lai_prev,pardr,pardf,suncos, &
      soil_moisture_root,wind_speed,soil_nox,mechanism_file,speciation_file, &
      temperature_history_k,par_history_wm2,days_between_lai,day_of_year,emissions,errmsg,errflg)
    integer, intent(in) :: ncol,nspecies,day_of_year
    real(kind_phys), intent(in) :: time,dt,temperature(:),lai(:),lai_prev(:),pardr(:),pardf(:),suncos(:)
    real(kind_phys), intent(in) :: soil_moisture_root(:),wind_speed(:),soil_nox(:)
    real(kind_phys), intent(in) :: temperature_history_k,par_history_wm2,days_between_lai
    character(len=512), intent(in) :: mechanism_file,speciation_file
    real(kind_phys), intent(inout) :: emissions(:,:)
    character(len=*), intent(out) :: errmsg
    integer, intent(out) :: errflg
    character(len=2048) :: options
    character(len=80) :: history_temp,history_par,history_days,doy
    real(kind_phys) :: inputs(ncol,9)
    integer, parameter :: manifest_species_count=36
    character(len=32), parameter :: species(manifest_species_count) = [character(len=32):: &
      'MEGAN_ISOP', &
      'MEGAN_TERP', &
      'MEGAN_APIN', &
      'MEGAN_PAR', &
      'MEGAN_OLE', &
      'MEGAN_ETH', &
      'MEGAN_FORM', &
      'MEGAN_ALD2', &
      'MEGAN_ALDX', &
      'MEGAN_TOL', &
      'MEGAN_XYL', &
      'MEGAN_IOLE', &
      'MEGAN_NR', &
      'MEGAN_MEOH', &
      'MEGAN_ETOH', &
      'MEGAN_ETHA', &
      'MEGAN_ACET', &
      'MEGAN_KET', &
      'MEGAN_PRPA', &
      'MEGAN_ETHY', &
      'MEGAN_BENZ', &
      'MEGAN_SESQ', &
      'MEGAN_NO', &
      'MEGAN_CO', &
      'MEGAN_FACD', &
      'MEGAN_AACD', &
      'MEGAN_SOAALK', &
      'MEGAN_IVOC', &
      'MEGAN_ACROLEIN', &
      'MEGAN_BUTADIENE13', &
      'MEGAN_HCN', &
      'MEGAN_CH4', &
      'MEGAN_HCOOH', &
      'MEGAN_CH3COOH', &
      'MEGAN_ISPD', &
      'MEGAN_UNREAC']
    real(c_double) :: inputs_c(ncol,9), outputs_c(ncol,nspecies)
    if (nspecies/=manifest_species_count .or. size(emissions,1)/=ncol .or. size(emissions,2)/=nspecies) then
      errmsg='CECE MEGAN3 CB6 species manifest/shape mismatch'
      errflg=1
      return
    end if
    if (size(temperature)/=ncol .or. &
        size(lai)/=ncol .or. &
        size(lai_prev)/=ncol .or. &
        size(pardr)/=ncol .or. &
        size(pardf)/=ncol .or. &
        size(suncos)/=ncol .or. &
        size(soil_moisture_root)/=ncol .or. &
        size(wind_speed)/=ncol .or. &
        size(soil_nox)/=ncol) then
      errmsg='CECE MEGAN3 input shape mismatch'
      errflg=1
      return
    end if
    ! JSON quoting rules are also valid YAML. Reject embedded quotes/newlines
    ! rather than constructing ambiguous YAML paths.
    if (index(trim(mechanism_file),'"')/=0 .or. index(trim(speciation_file),'"')/=0 .or. &
        index(trim(mechanism_file),achar(10))/=0 .or. index(trim(speciation_file),achar(10))/=0 .or. &
        index(trim(mechanism_file),achar(92))/=0 .or. index(trim(speciation_file),achar(92))/=0) then
      errmsg='Unsupported quote, newline or backslash in CECE parameter path'
      errflg=1
      return
    end if
    write(history_temp,'(es24.16)') temperature_history_k
    write(history_par,'(es24.16)') par_history_wm2
    write(history_days,'(es24.16)') days_between_lai
    write(doy,'(i0)') day_of_year
    options='mechanism_file: "'//trim(mechanism_file)//'"'//achar(10)// &
        'speciation_file: "'//trim(speciation_file)//'"'//achar(10)// &
        'temperature_history_k: '//trim(history_temp)//achar(10)// &
        'par_history_wm2: '//trim(history_par)//achar(10)// &
        'days_between_lai: '//trim(history_days)//achar(10)//'day_of_year: '//trim(doy)
    inputs(:,1)=temperature
    inputs(:,2)=lai
    inputs(:,3)=lai_prev
    inputs(:,4)=pardr
    inputs(:,5)=pardf
    inputs(:,6)=suncos
    inputs(:,7)=soil_moisture_root
    inputs(:,8)=wind_speed
    inputs(:,9)=soil_nox
    inputs_c=real(inputs,c_double)
    outputs_c=0.0_c_double
    call evaluate_scheme('megan3',trim(options),ncol,1,1, &
        [character(len=32)::'temperature','leaf_area_index','leaf_area_index_prev', &
        'par_direct','par_diffuse','solar_cosine','soil_moisture_root','wind_speed','soil_nox_emissions'], &
        inputs_c,species,outputs_c,real(time,c_double),real(dt,c_double),errmsg,errflg)
    if (errflg==0) emissions=real(outputs_c,kind_phys)
  end subroutine
end module
