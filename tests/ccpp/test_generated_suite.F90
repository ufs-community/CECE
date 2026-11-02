! Actual capgen-generated suite acceptance; no scheme/science stubs.
program test_generated_suite
  use, intrinsic :: ieee_arithmetic, only: ieee_value, ieee_quiet_nan
  use cece_test_host
  use cece_test_ccpp_cap
  use cece_megan3_driver, only: cece_megan3_driver_run
  implicit none
  character(len=1024) :: root, output_file
  real(kind_phys), allocatable :: first(:,:)
  integer :: i, unit
  call get_command_argument(1,root)
  call get_command_argument(2,output_file)
  call setup_host(trim(root))
  call ccpp_register('cece_suite',ccpp_error_code,ccpp_error_message)
  call check()
  call ccpp_init('cece_suite',ccpp_error_code,ccpp_error_message)
  call check()
  call initialize_group('native')
  call initialize_group('production')
  call begin_group('native')
  call begin_group('production')
  ! Interleave unequal active column blocks. No context may leak column history.
  call run_group('native',1,1)
  call run_group('native',2,3)
  if (any(abs(cece_example_nox-6.0_kind_phys)>1.e-14_kind_phys)) error stop 'native scheme numeric mismatch'
  call run_group('production',1,1)
  call run_group('production',2,3)
  if (any(surface_soil_no_mass_flux<=0)) error stop 'BDSNP must produce positive soil NO'
  if (.not.any(cece_cb6_surface_species_mass_flux>0)) error stop 'MEGAN3 must produce positive emissions'
  first=cece_cb6_surface_species_mass_flux
  ! A bad input must not publish any partial output.
  surface_air_temperature(2)=ieee_value(0.0_kind_phys,ieee_quiet_nan)
  call ccpp_physics_run('cece_suite','production',1,3,1,1,1,ccpp_error_code,ccpp_error_message)
  if (ccpp_error_code==0) error stop 'nonfinite input was accepted'
  if (any(first/=cece_cb6_surface_species_mass_flux)) error stop 'failed MEGAN3 changed host output'
  surface_air_temperature=303.0_kind_phys
  call run_group('production',1,3)
  if (any(abs(first-cece_cb6_surface_species_mass_flux)>1.e-25_kind_phys)) error stop 'repeated forcing changed flux'
  if (len_trim(output_file)>0) then
    open(newunit=unit,file=trim(output_file),status='replace')
    do i=1,cece_cb6_species_dimension
      write(unit,'(*(es25.16,1x))') cece_cb6_surface_species_mass_flux(:,i)
    end do
    write(unit,'(*(es25.16,1x))') surface_soil_no_mass_flux
    close(unit)
  end if
  call end_group('native')
  call end_group('production')
  call finish_group('native')
  call finish_group('production')
  call ccpp_final('cece_suite',ccpp_error_code,ccpp_error_message)
  call check()
  print *, 'generated CCPP suite checks passed'
contains
  subroutine check()
    if (ccpp_error_code/=0) then
      print *,trim(ccpp_error_message)
      error stop 'CCPP operation failed'
    end if
  end subroutine
  subroutine initialize_group(name)
    character(len=*),intent(in)::name
    call ccpp_physics_init('cece_suite',name,1,3,1,1,1,ccpp_error_code,ccpp_error_message)
    call check()
  end subroutine
  subroutine begin_group(name)
    character(len=*),intent(in)::name
    call ccpp_physics_timestep_init('cece_suite',name,1,3,1,1,1,ccpp_error_code,ccpp_error_message)
    call check()
  end subroutine
  subroutine run_group(name,lower,upper)
    character(len=*),intent(in)::name
    integer,intent(in)::lower,upper
    call ccpp_physics_run('cece_suite',name,lower,upper,1,1,1,ccpp_error_code,ccpp_error_message)
    call check()
  end subroutine
  subroutine end_group(name)
    character(len=*),intent(in)::name
    call ccpp_physics_timestep_final('cece_suite',name,1,3,1,1,1,ccpp_error_code,ccpp_error_message)
    call check()
  end subroutine
  subroutine finish_group(name)
    character(len=*),intent(in)::name
    call ccpp_physics_final('cece_suite',name,1,3,1,1,1,ccpp_error_code,ccpp_error_message)
    call check()
  end subroutine
end program
