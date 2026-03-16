#!/usr/bin/env python3
"""
Physics Scheme Generator for ACES

This script generates physics scheme scaffolding from YAML configuration files.
It creates C++ header, implementation, and optional Fortran kernel files with
comprehensive documentation and examples.

Usage:
    ./generate_physics_scheme.py <config.yaml>

Configuration Format:
    scheme:
      name: CustomScheme
      language: cpp  # or fortran
      description: "Description of the scheme"

    imports:
      - name: temperature
        var_name: temp
        units: K
        dimensions: 3D

    exports:
      - name: emissions
        var_name: emis
        units: kg/m2/s
        dimensions: 2D

    options:
      - name: emission_factor
        type: double
        default: 1.0e-6
        description: "Base emission factor"

    diagnostics:
      - name: intermediate_var
        units: dimensionless
        description: "Intermediate computation result"
"""

import yaml
import sys
import os
from pathlib import Path
from jinja2 import Environment, FileSystemLoader, TemplateNotFound


def validate_config(config):
    """Validate the scheme configuration YAML."""
    errors = []

    # Check required top-level keys
    if "scheme" not in config:
        errors.append("Missing required 'scheme' section")
        return errors

    scheme = config["scheme"]
    if "name" not in scheme:
        errors.append("Missing required 'scheme.name'")
    if "description" not in scheme:
        errors.append("Missing required 'scheme.description'")

    # Validate scheme name format
    if "name" in scheme:
        name = scheme["name"]
        if not name[0].isupper():
            errors.append(f"Scheme name '{name}' must start with uppercase letter")
        if not name.replace("_", "").isalnum():
            errors.append(f"Scheme name '{name}' contains invalid characters")

    # Validate imports
    if "imports" in config:
        for i, imp in enumerate(config["imports"]):
            if "name" not in imp:
                errors.append(f"Import {i}: missing 'name'")
            if "var_name" not in imp:
                errors.append(f"Import {i}: missing 'var_name'")
            if "units" not in imp:
                errors.append(f"Import {i}: missing 'units'")

    # Validate exports
    if "exports" in config:
        for i, exp in enumerate(config["exports"]):
            if "name" not in exp:
                errors.append(f"Export {i}: missing 'name'")
            if "var_name" not in exp:
                errors.append(f"Export {i}: missing 'var_name'")
            if "units" not in exp:
                errors.append(f"Export {i}: missing 'units'")

    # Validate options
    if "options" in config:
        for i, opt in enumerate(config["options"]):
            if "name" not in opt:
                errors.append(f"Option {i}: missing 'name'")
            if "type" not in opt:
                errors.append(f"Option {i}: missing 'type'")
            if "default" not in opt:
                errors.append(f"Option {i}: missing 'default'")
            if "description" not in opt:
                errors.append(f"Option {i}: missing 'description'")

    return errors


def generate_scheme(config_path):
    """Generate physics scheme files from configuration."""
    if not os.path.exists(config_path):
        print(f"Error: Config file {config_path} not found.")
        sys.exit(1)

    # Load and validate configuration
    with open(config_path, "r") as f:
        config = yaml.safe_load(f)

    errors = validate_config(config)
    if errors:
        print("Configuration validation errors:")
        for error in errors:
            print(f"  - {error}")
        sys.exit(1)

    # Extract configuration
    scheme = config["scheme"]
    class_name = scheme["name"]
    scheme_name = scheme.get("scheme_name", class_name.lower())
    description = scheme["description"]
    language = scheme.get("language", "cpp")
    generate_fortran = language == "fortran" or scheme.get("generate_fortran", False)

    header_name = f"aces_{scheme_name}.hpp"
    cpp_name = f"aces_{scheme_name}.cpp"
    f90_name = f"{scheme_name}_kernel.F90"

    # Prepare template context
    context = {
        "class_name": class_name,
        "scheme_name": scheme_name,
        "description": description,
        "header_name": header_name,
        "imports": config.get("imports", []),
        "exports": config.get("exports", []),
        "options": config.get("options", []),
        "diagnostics": config.get("diagnostics", []),
    }

    # Set up Jinja2 environment
    env = Environment(loader=FileSystemLoader("scripts/templates"))

    # Create output directories if they don't exist
    Path("include/aces/physics").mkdir(parents=True, exist_ok=True)
    Path("src/physics").mkdir(parents=True, exist_ok=True)

    # Generate C++ Header
    try:
        hpp_template = env.get_template("physics_scheme.hpp.jinja2")
        hpp_path = f"include/aces/physics/{header_name}"
        with open(hpp_path, "w") as f:
            f.write(hpp_template.render(context))
        print(f"✓ Generated: {hpp_path}")
    except TemplateNotFound as e:
        print(f"Error: Template not found: {e}")
        sys.exit(1)

    # Generate C++ Implementation
    try:
        cpp_template = env.get_template("physics_scheme.cpp.jinja2")
        cpp_path = f"src/physics/{cpp_name}"
        with open(cpp_path, "w") as f:
            f.write(cpp_template.render(context))
        print(f"✓ Generated: {cpp_path}")
    except TemplateNotFound as e:
        print(f"Error: Template not found: {e}")
        sys.exit(1)

    # Generate Fortran Kernel (optional)
    if generate_fortran:
        try:
            f90_template = env.get_template("physics_kernel.F90.jinja2")
            f90_path = f"src/physics/{f90_name}"
            with open(f90_path, "w") as f:
                f.write(f90_template.render(context))
            print(f"✓ Generated: {f90_path}")
        except TemplateNotFound as e:
            print(f"Error: Template not found: {e}")
            sys.exit(1)

    # Generate CMakeLists.txt integration instructions
    print("\n" + "=" * 70)
    print("Next Steps:")
    print("=" * 70)
    print(f"\n1. Add the following to CMakeLists.txt in the src/physics section:")
    print(f"   add_library(aces_physics_{scheme_name} {cpp_name})")
    print(f"   target_link_libraries(aces_physics_{scheme_name} PUBLIC aces_core)")
    print(f"   target_link_libraries(aces_core PUBLIC aces_physics_{scheme_name})")

    if generate_fortran:
        print(f"\n2. If using Fortran kernel, add to CMakeLists.txt:")
        print(f"   enable_language(Fortran)")
        print(f"   add_library(aces_physics_{scheme_name}_fortran {f90_name})")
        print(f"   target_link_libraries(aces_physics_{scheme_name} PUBLIC aces_physics_{scheme_name}_fortran)")

    print(f"\n3. Implement the physics logic in:")
    print(f"   - {cpp_path} (Run method)")
    if generate_fortran:
        print(f"   - {f90_path} (Fortran kernel)")

    print(f"\n4. Configure the scheme in your YAML config:")
    print(f"   physics_schemes:")
    print(f"     - name: {scheme_name}")
    print(f"       options:")
    for opt in config.get("options", []):
        print(f"         {opt['name']}: {opt['default']}")

    print(f"\n5. Run tests to verify the scheme compiles and runs:")
    print(f"   cd build && ctest --output-on-failure")

    print("\n" + "=" * 70)
    print(f"Successfully generated scheme: {class_name} ({scheme_name})")
    print("=" * 70)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: ./generate_physics_scheme.py <config.yaml>")
        print("\nFor more information, see the docstring in this file.")
        sys.exit(1)
    generate_scheme(sys.argv[1])
