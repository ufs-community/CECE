! Test host precision only. Real UFS integrations supply their own machine module.
module machine
  use iso_c_binding, only: c_double
  implicit none
  integer, parameter :: kind_phys = c_double
end module
