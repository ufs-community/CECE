# Copyright 2024 UFS Community
# SPDX-License-Identifier: Apache-2.0

import os
from typing import ClassVar

from spack.package import *


class Cece(CMakePackage):
    """CECE: Community Emissions Computing Engine.

    A high-performance, performance-portable emissions compute component
    for Earth System Models. Built using C++20 and the Kokkos programming
    model, designed to run efficiently on both multi-core CPUs and GPUs.
    """

    homepage = "https://github.com/ufs-community/CECE"
    git = "https://github.com/ufs-community/CECE.git"

    maintainers("ufs-community")

    version("develop", branch="main")
    # Add tagged releases as they become available:
    # version("1.0.0", tag="v1.0.0")

    # --- Variants ---
    variant(
        "openmp", default=True, description="Enable OpenMP execution space in Kokkos"
    )
    variant(
        "cuda", default=False, description="Enable CUDA execution space for NVIDIA GPUs"
    )
    variant("hip", default=False, description="Enable HIP execution space for AMD GPUs")
    variant(
        "fortran",
        default=True,
        description="Enable Fortran support (NUOPC cap, CeceIO)",
    )
    variant("python", default=False, description="Build Python bindings")

    # --- Dependencies ---
    depends_on("cmake@3.20:", type="build")
    depends_on("mpi")
    depends_on("esmf", when="+fortran")
    depends_on("parallelio", when="+fortran")
    depends_on("netcdf-c")
    depends_on("netcdf-fortran", when="+fortran")

    # Kokkos and yaml-cpp are formally required dependencies
    depends_on("kokkos@5.1.1:")
    depends_on("kokkos+openmp", when="+openmp")
    depends_on("kokkos+cuda", when="+cuda")
    depends_on("kokkos+hip", when="+hip")
    depends_on("yaml-cpp@0.8.0:")

    # On macOS with Apple Clang, OpenMP requires llvm-openmp
    depends_on("llvm-openmp", when="+openmp platform=darwin", type=("build", "link"))

    depends_on("python@3.8:", when="+python", type=("build", "run"))

    # Compiler requirements — C++20 support needed
    conflicts("%gcc@:10", msg="CECE requires C++20 support (GCC 11+)")
    conflicts("%clang@:13", msg="CECE requires C++20 support (Clang 14+)")
    conflicts("%oneapi@:2021", msg="CECE requires C++20 support (oneAPI 2022+)")

    # Conflicts
    conflicts("+cuda +hip", msg="Cannot enable both CUDA and HIP simultaneously")

    # Only build the library and standalone app, skip test targets and docs
    build_targets: ClassVar[list] = ["cece", "cece_nuopc_app"]
    install_targets: ClassVar[list] = ["install"]

    def cmake_args(self):
        args = [
            self.define("CMAKE_CXX_STANDARD", "20"),
            self.define("FETCHCONTENT_TRY_FIND_PACKAGE_MODE", "ALWAYS"),
            self.define("Kokkos_ENABLE_SERIAL", True),
            self.define_from_variant("Kokkos_ENABLE_OPENMP", "openmp"),
            self.define_from_variant("Kokkos_ENABLE_CUDA", "cuda"),
            self.define_from_variant("Kokkos_ENABLE_HIP", "hip"),
            self.define_from_variant("BUILD_PYTHON_BINDINGS", "python"),
        ]

        # On macOS with Apple Clang, help CMake find OpenMP
        if (
            "+openmp" in self.spec
            and self.spec.satisfies("platform=darwin")
            and "llvm-openmp" in self.spec
        ):
            libomp = self.spec["llvm-openmp"]
            omp_inc = libomp.prefix.include
            omp_lib = join_path(libomp.prefix.lib, "libomp.dylib")
            args.extend([
                self.define("OpenMP_C_FLAGS", f"-Xpreprocessor -fopenmp -I{omp_inc}"),
                self.define("OpenMP_CXX_FLAGS", f"-Xpreprocessor -fopenmp -I{omp_inc}"),
                self.define("OpenMP_C_LIB_NAMES", "omp"),
                self.define("OpenMP_CXX_LIB_NAMES", "omp"),
                self.define("OpenMP_omp_LIBRARY", omp_lib),
            ])

        # Workaround for bundled yaml-cpp requiring old cmake_minimum_required
        args.append(self.define("CMAKE_POLICY_VERSION_MINIMUM", "3.5"))

        # Point CMake at ESMF
        if "+fortran" in self.spec:
            esmf = self.spec["esmf"]
            esmf_mk = join_path(esmf.prefix.lib, "esmf.mk")
            if not os.path.exists(esmf_mk):
                # Some installations put it under lib64
                esmf_mk = join_path(esmf.prefix.lib64, "esmf.mk")
            args.append(self.define("ESMFMKFILE", esmf_mk))

            # PIO
            if "parallelio" in self.spec:
                args.append(self.define("PIO_ROOT", self.spec["parallelio"].prefix))

            # NetCDF-Fortran
            if "netcdf-fortran" in self.spec:
                args.append(
                    self.define("NetCDFF_ROOT", self.spec["netcdf-fortran"].prefix)
                )
        else:
            # Force Fortran off by pointing to a non-existent compiler
            args.append(self.define("CMAKE_Fortran_COMPILER", "NOTFOUND"))

        # NetCDF-C
        if "netcdf-c" in self.spec:
            args.append(self.define("NetCDF_ROOT", self.spec["netcdf-c"].prefix))

        # CUDA architecture
        if "+cuda" in self.spec:
            cuda_arch = self.spec.variants["cuda_arch"].value
            if cuda_arch:
                args.append(self.define("Kokkos_ARCH_" + cuda_arch[0].upper(), True))

        return args

    def setup_run_environment(self, env):
        env.prepend_path("PATH", self.prefix.bin)
        if "+python" in self.spec:
            env.prepend_path("PYTHONPATH", join_path(self.prefix.lib, "python"))

    def setup_dependent_build_environment(self, env, dependent_spec):
        env.set("CECE_ROOT", self.prefix)
