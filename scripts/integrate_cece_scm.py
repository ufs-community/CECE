import sys
import os
import re


def integrate(scm_root, cece_root_relative_to_scm):
    print(f"Integrating CECE from {cece_root_relative_to_scm} into SCM at {scm_root}")
    scm_root = os.path.abspath(scm_root)
    cece_src_relative = os.path.join(cece_root_relative_to_scm, "src", "ccpp")
    cece_abs_path = os.path.join(scm_root, cece_root_relative_to_scm)
    cece_build_dir = os.path.join(cece_abs_path, "build")

    # 1. Update ccpp_prebuild_config.py
    config_path = os.path.join(scm_root, "ccpp", "config", "ccpp_prebuild_config.py")
    if os.path.exists(config_path):
        with open(config_path, "r") as f:
            content = f.read()

        # Add CECE state to VARIABLE_DEFINITION_FILES
        cece_state_file = os.path.join(cece_src_relative, "cece_ccpp_state.F90")
        if cece_state_file not in content:
            content = content.replace(
                "VARIABLE_DEFINITION_FILES = [",
                f"VARIABLE_DEFINITION_FILES = [\n    '{cece_state_file}',",
            )

        # Add CECE to TYPEDEFS_NEW_METADATA
        if "'cece_ccpp_state' :" not in content:
            content = content.replace(
                "TYPEDEFS_NEW_METADATA = {",
                "TYPEDEFS_NEW_METADATA = {\n    'cece_ccpp_state' : {\n        'cece_ccpp_state' : '',\n    },",
            )

        # Add CECE schemes to SCHEME_FILES
        cece_schemes = [
            "cece_ccpp_dms.F90",
            "cece_ccpp_dust.F90",
            "cece_ccpp_example.F90",
            "cece_ccpp_fengsha.F90",
            "cece_ccpp_ginoux.F90",
            "cece_ccpp_k14.F90",
            "cece_ccpp_lightning.F90",
            "cece_ccpp_megan.F90",
            "cece_ccpp_sea_salt.F90",
            "cece_ccpp_soil_nox.F90",
            "cece_ccpp_stacking.F90",
            "cece_ccpp_volcano.F90",
        ]

        scheme_paths = [os.path.join(cece_src_relative, s) for s in cece_schemes]
        scheme_block = "\n".join([f"    '{s}'," for s in scheme_paths])

        if scheme_paths[0] not in content:
            content = content.replace(
                "SCHEME_FILES = [", f"SCHEME_FILES = [\n{scheme_block}"
            )

        with open(config_path, "w") as f:
            f.write(content)
        print(f"Updated {config_path}")
    else:
        print(f"CRITICAL: {config_path} not found")

    # 2. Update SCM CMakeLists.txt to link CECE
    cmake_path = os.path.join(scm_root, "scm", "src", "CMakeLists.txt")
    if os.path.exists(cmake_path):
        with open(cmake_path, "r") as f:
            cmake_content = f.read()

        if "CECE Integration" not in cmake_content:
            # Dynamically find library files
            libcece_path = ""
            libkokkos_path = ""
            libyaml_path = ""

            for root, _, files in os.walk(cece_build_dir):
                for f in files:
                    if f == "libcece.so":
                        libcece_path = os.path.join(root, f)
                    if f == "libkokkoscore.a":
                        libkokkos_path = os.path.join(root, f)
                    if f == "libyaml-cpp.a":
                        libyaml_path = os.path.join(root, f)

            # Convert to relative paths from scm/src
            def to_cmake_path(abs_path):
                if not abs_path:
                    return ""
                return abs_path.replace(scm_root, "${CMAKE_SOURCE_DIR}/../..")

            libcece_cmake = to_cmake_path(libcece_path)
            libkokkos_cmake = to_cmake_path(libkokkos_path)
            libyaml_cmake = to_cmake_path(libyaml_path)

            insertion = f"""
# CECE Integration
set(CECE_BUILD_DIR "${{CMAKE_SOURCE_DIR}}/../../{cece_root_relative_to_scm}/build")
include_directories("${{CMAKE_SOURCE_DIR}}/../../{cece_root_relative_to_scm}/include")
include_directories("${{CECE_BUILD_DIR}}/_deps/kokkos-src/core/src")
include_directories("${{CECE_BUILD_DIR}}/_deps/yaml-cpp-src/include")
"""
            cmake_content = cmake_content.replace(
                "project(scm", insertion + "\nproject(scm"
            )

            # Link using full paths to avoid library search issues
            cmake_content = cmake_content.replace(
                "TARGET_LINK_LIBRARIES(scm ccpp_physics)",
                f'TARGET_LINK_LIBRARIES(scm ccpp_physics "{libcece_cmake}" "{libkokkos_cmake}" "{libyaml_cmake}")',
            )

        with open(cmake_path, "w") as f:
            f.write(cmake_content)
        print(f"Updated {cmake_path}")

    # 3. Add cece_configuration_file_path to scm_type_defs.F90 to satisfy CCPP dependencies
    scm_type_defs_meta = os.path.join(scm_root, "scm", "src", "scm_type_defs.meta")
    if os.path.exists(scm_type_defs_meta):
        with open(scm_type_defs_meta, "r") as f:
            meta_content = f.read()

        if "cece_configuration_file_path" not in meta_content:
            new_var_meta = """
[cece_config_path]
  standard_name = cece_configuration_file_path
  long_name = path to CECE YAML configuration file
  units = none
  dimensions = ()
  type = character
  kind = len=512
  intent = inout
"""
            parts = re.split(r"(\[ccpp-arg-table\])", meta_content)
            for i in range(len(parts)):
                if parts[i] == "[ccpp-arg-table]" and i + 1 < len(parts):
                    if "name = physics_type" in parts[i + 1]:
                        parts[i + 1] = parts[i + 1].replace(
                            "type = ddt", "type = ddt" + new_var_meta
                        )
                        break
            meta_content = "".join(parts)

            with open(scm_type_defs_meta, "w") as f:
                f.write(meta_content)
            print(f"Updated {scm_type_defs_meta}")

    scm_type_defs_f90 = os.path.join(scm_root, "scm", "src", "scm_type_defs.F90")
    if os.path.exists(scm_type_defs_f90):
        with open(scm_type_defs_f90, "r") as f:
            f90_content = f.read()

        if "cece_config_path" not in f90_content:
            f90_content = f90_content.replace(
                "type physics_type",
                "type physics_type\n    character(len=512) :: cece_config_path = 'cece_config_scm.yaml'",
            )
            with open(scm_type_defs_f90, "w") as f:
                f.write(f90_content)
            print(f"Updated {scm_type_defs_f90}")

    # 4. Patch scm.F90 to call cece_emissions group
    scm_f90_path = os.path.join(scm_root, "scm", "src", "scm.F90")
    if os.path.exists(scm_f90_path):
        with open(scm_f90_path, "r") as f:
            scm_f90_content = f.read()

        if 'group_name="cece_emissions"' not in scm_f90_content:
            cece_call = """
    ! CECE emissions
    call ccpp_physics_run(cdata, suite_name=trim(adjustl(scm_state%physics_suite_name)), group_name="cece_emissions", ierr=ierr)
    if (ierr/=0) then
        write(error_unit,'(a,i0,a)') 'An error occurred in ccpp_physics_run for group cece_emissions: ' // trim(cdata%errmsg) // '. Exiting...'
        error stop
    end if
"""
            # Insert before radiation group in the time loop and initial step
            scm_f90_content = scm_f90_content.replace(
                "! radiation group", cece_call + "    ! radiation group"
            )
            with open(scm_f90_path, "w") as f:
                f.write(scm_f90_content)
            print(f"Updated {scm_f90_path}")

    # 5. Update suite_info.py to include defaults for the new suite
    for suite_info_rel_path in ["scm/src/suite_info.py", "scm/etc/suite_info.py"]:
        suite_info_path = os.path.join(scm_root, suite_info_rel_path)
        print(f"DEBUG: Checking for suite_info.py at {suite_info_path}")
        if os.path.exists(suite_info_path):
            with open(suite_info_path, "r") as f:
                suite_info_content = f.read()

            if "SCM_GFS_v16_CECE" in suite_info_content:
                print(f"DEBUG: SCM_GFS_v16_CECE already in {suite_info_path}")
            else:
                print(f"DEBUG: Patching {suite_info_path}")

                # Format A: suite_list.append(suite('NAME', ...))
                match = re.search(
                    r"suite_list\.append\(suite\('SCM_GFS_v16'.*?\)\)",
                    suite_info_content,
                )
                if match:
                    gfs_line = match.group(0)
                    # SCM_GFS_v16_CECE
                    cece_line = gfs_line.replace("SCM_GFS_v16", "SCM_GFS_v16_CECE")
                    # SCM_GFS_v16_CECE_ps
                    cece_ps_line = cece_line.replace(
                        "SCM_GFS_v16_CECE", "SCM_GFS_v16_CECE_ps"
                    )

                    suite_info_content = suite_info_content.replace(
                        gfs_line, gfs_line + "\n" + cece_line + "\n" + cece_ps_line
                    )
                    print("DEBUG: Cloned SCM_GFS_v16 in suite_list (List format)")
                else:
                    # Format B: suites = { 'NAME': { ... }, }
                    pattern = r"(['\"]SCM_GFS_v16['\"]\s*:\s*\{)"
                    match = re.search(pattern, suite_info_content)
                    if match:
                        start_idx = match.start()
                        brace_start = suite_info_content.find("{", start_idx)
                        if brace_start != -1:
                            brace_count = 0
                            end_idx = -1
                            for i in range(brace_start, len(suite_info_content)):
                                if suite_info_content[i] == "{":
                                    brace_count += 1
                                elif suite_info_content[i] == "}":
                                    brace_count -= 1
                                    if brace_count == 0:
                                        end_idx = i + 1
                                        break
                            if end_idx != -1:
                                base_entry = suite_info_content[start_idx:end_idx]
                                cece_entry = base_entry.replace(
                                    "SCM_GFS_v16", "SCM_GFS_v16_CECE", 1
                                )
                                cece_ps_entry = cece_entry.replace(
                                    "SCM_GFS_v16_CECE", "SCM_GFS_v16_CECE_ps", 1
                                )
                                suite_info_content = (
                                    suite_info_content[:end_idx]
                                    + ",\n    "
                                    + cece_entry
                                    + ",\n    "
                                    + cece_ps_entry
                                    + suite_info_content[end_idx:]
                                )
                                print(
                                    "DEBUG: Cloned SCM_GFS_v16 in suites dict (Dict format)"
                                )

                with open(suite_info_path, "w") as f:
                    f.write(suite_info_content)
                print(f"Updated {suite_info_path}")
        else:
            print(f"DEBUG: {suite_info_path} not found")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(
            "Usage: python integrate_cece_scm.py <scm_root> <cece_root_relative_to_scm>"
        )
        sys.exit(1)
    integrate(sys.argv[1], sys.argv[2])
