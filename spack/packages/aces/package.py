# Copyright 2013-2024 Lawrence Livermore National Security, LLC and other
# Spack Project Developers. See the top-level COPYRIGHT file for details.
#
# SPDX-License-Identifier: (Apache-2.0 OR MIT)

from spack.package import *


class Aces(CMakePackage):
    """Accelerated Component for Emission System (ACES) is a C++17 emissions
    compute component designed for high performance using Kokkos and ESMF."""

    homepage = "https://github.com/bbakernoaa/aces"
    git = "https://github.com/bbakernoaa/aces.git"

    maintainers("jules")

    license("Apache-2.0")

    version("main", branch="main")
    version("0.1.0", tag="v0.1.0")

    variant("fortran", default=True, description="Enable Fortran support")
    variant("tests", default=False, description="Build tests")

    depends_on("cxx", type="build")  # require C++17
    depends_on("fortran", type="build", when="+fortran")

    depends_on("cmake@3.20:", type="build")
    depends_on("kokkos@4.2.00:")
    depends_on("yaml-cpp@0.8.0:")
    depends_on("esmf@8.4.0:")
    depends_on("mpi")
    depends_on("cdeps")
    depends_on("netcdf-fortran")
    depends_on("pio")
    depends_on("googletest@1.14.0:", type="test", when="+tests")

    def cmake_args(self):
        args = [
            self.define_from_variant("BUILD_TESTING", "tests"),
        ]

        # ACES currently auto-detects Fortran if compiler is present,
        # but we can pass it explicitly if we want.
        # The CMakeLists.txt uses check_language(Fortran).

        return args
