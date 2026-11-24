module cece_test_host
 use machine, only: kind_phys
 implicit none
 integer :: horizontal_dimension
 real(kind_phys) :: cece_interval_start_seconds_since_unix_epoch
 real(kind_phys) :: time_step_for_physics
 integer :: vertical_layer_dimension
 real(kind_phys), allocatable :: cece_base_anthropogenic_nox(:,:)
 real(kind_phys) :: cece_example_multiplier
 real(kind_phys), allocatable :: cece_example_nox(:,:)
 real(kind_phys), allocatable :: soil_temperature_for_cece_yl95(:)
 real(kind_phys), allocatable :: water_filled_pore_space_fraction_for_cece_yl95(:)
 real(kind_phys), allocatable :: surface_soil_no_mass_flux(:)
 integer :: cece_cb6_species_dimension
 real(kind_phys), allocatable :: surface_air_temperature(:)
 real(kind_phys), allocatable :: leaf_area_index(:)
 real(kind_phys), allocatable :: previous_leaf_area_index_for_cece(:)
 real(kind_phys), allocatable :: surface_downwelling_direct_par_flux(:)
 real(kind_phys), allocatable :: surface_downwelling_diffuse_par_flux(:)
 real(kind_phys), allocatable :: cosine_of_solar_zenith_angle(:)
 real(kind_phys), allocatable :: root_zone_soil_water_fraction_for_cece(:)
 real(kind_phys), allocatable :: surface_wind_speed(:)
 character(len=512) :: cece_mechanism_file
 character(len=512) :: cece_speciation_file
 real(kind_phys) :: cece_effective_temperature_history
 real(kind_phys) :: cece_effective_par_history
 real(kind_phys) :: cece_days_between_lai
 integer :: day_of_year
 real(kind_phys), allocatable :: cece_cb6_surface_species_mass_flux(:,:)
 character(len=512) :: ccpp_error_message
 integer :: ccpp_error_code
 character(len=256) :: suite_name, group_name
 integer :: horizontal_loop_begin, horizontal_loop_end, thread_number, number_of_threads, number_of_physics_threads
contains
 subroutine setup_host(root)
 character(len=*), intent(in) :: root
 ccpp_error_message=''
 ccpp_error_code=0
 horizontal_dimension=3
 cece_interval_start_seconds_since_unix_epoch=0.0_kind_phys
 time_step_for_physics=3600.0_kind_phys
 vertical_layer_dimension=2
 cece_example_multiplier=3.0_kind_phys
 cece_cb6_species_dimension=36
 cece_mechanism_file=''
 cece_speciation_file=''
 cece_effective_temperature_history=297.0_kind_phys
 cece_effective_par_history=400.0_kind_phys
 cece_days_between_lai=30.0_kind_phys
 day_of_year=180
 suite_name=''
 group_name=''
 horizontal_loop_begin=1
 horizontal_loop_end=3
 thread_number=1
 number_of_threads=1
 number_of_physics_threads=1
 allocate(cece_base_anthropogenic_nox(horizontal_dimension,vertical_layer_dimension))
 cece_base_anthropogenic_nox=2.0_kind_phys
 allocate(cece_example_nox(horizontal_dimension,vertical_layer_dimension))
 cece_example_nox=-99.0_kind_phys
 allocate(soil_temperature_for_cece_yl95(horizontal_dimension))
 soil_temperature_for_cece_yl95=303.0_kind_phys
 allocate(water_filled_pore_space_fraction_for_cece_yl95(horizontal_dimension))
 water_filled_pore_space_fraction_for_cece_yl95=0.2_kind_phys
 allocate(surface_soil_no_mass_flux(horizontal_dimension))
 surface_soil_no_mass_flux=-99.0_kind_phys
 allocate(surface_air_temperature(horizontal_dimension))
 surface_air_temperature=303.0_kind_phys
 allocate(leaf_area_index(horizontal_dimension))
 leaf_area_index=3.0_kind_phys
 allocate(previous_leaf_area_index_for_cece(horizontal_dimension))
 previous_leaf_area_index_for_cece=3.0_kind_phys
 allocate(surface_downwelling_direct_par_flux(horizontal_dimension))
 surface_downwelling_direct_par_flux=300.0_kind_phys
 allocate(surface_downwelling_diffuse_par_flux(horizontal_dimension))
 surface_downwelling_diffuse_par_flux=100.0_kind_phys
 allocate(cosine_of_solar_zenith_angle(horizontal_dimension))
 cosine_of_solar_zenith_angle=0.8_kind_phys
 allocate(root_zone_soil_water_fraction_for_cece(horizontal_dimension))
 root_zone_soil_water_fraction_for_cece=0.2_kind_phys
 allocate(surface_wind_speed(horizontal_dimension))
 surface_wind_speed=2.0_kind_phys
 allocate(cece_cb6_surface_species_mass_flux(horizontal_dimension,cece_cb6_species_dimension))
 cece_cb6_surface_species_mass_flux=-99.0_kind_phys
 cece_mechanism_file=trim(root)//'/data/speciation/spc_cb6.yaml'
 cece_speciation_file=trim(root)//'/data/speciation/map_cb6.yaml'
 end subroutine
end module
