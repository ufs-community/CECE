# TIDE Streams Configuration Parser

## Overview

The TIDE Streams Configuration Parser provides functionality to parse, validate, and serialize YAML format streams files used by TIDE for data ingestion in CECE. This enables hybrid data ingestion where static emission inventories from NetCDF files are combined with live meteorological fields from ESMF.

## Features

- **ESMF Config Format Parsing**: Reads streams files in ESMF Config format
- **Comprehensive Validation**: Validates file paths, variables, and interpolation modes
- **Round-Trip Serialization**: Supports writing configurations back to ESMF Config format
- **Detailed Error Messages**: Provides actionable error messages for configuration issues
- **NetCDF Integration**: Validates that variables exist in specified NetCDF files

## YAML Format

The YAML format for TIDE streams uses a simple key-value syntax:

```
stream::<stream_name>
  file_paths = <path1>, <path2>, ...
  variables = <var_in_file:var_in_model>, ...
  taxmode = <cycle|extend|limit>
  tintalgo = <none|linear|nearest|lower|upper>
  mapalgo = <bilinear|patch|...>
  yearFirst = <year>
  yearLast = <year>
  yearAlign = <year>
  offset = <seconds>
  meshfile = <path>
  lev_dimname = <dimension_name>
::
```

### Required Attributes

- `file_paths`: Comma-separated list of NetCDF file paths
- `variables`: Comma-separated list of variable mappings

### Optional Attributes

- `taxmode`: Time axis mode (default: "cycle")
  - `cycle`: Cycle through data years
  - `extend`: Extend first/last values beyond data range
  - `limit`: Limit to data range
- `tintalgo`: Temporal interpolation algorithm (default: "linear")
  - `none`: No interpolation
  - `linear`: Linear interpolation between time steps
  - `nearest`: Nearest neighbor
  - `lower`: Use lower bound
  - `upper`: Use upper bound
- `mapalgo`: Spatial mapping algorithm (default: "bilinear")
- `dtlimit`: Delta time limit in seconds (default: 1500000000)
- `yearFirst`: First year in data (default: 1)
- `yearLast`: Last year in data (default: 1)
- `yearAlign`: Year to align with model time (default: 1)
- `offset`: Time offset in seconds (default: 0)
- `meshfile`: Path to source mesh file (optional)
- `lev_dimname`: Name of vertical dimension (default: "lev")

## Usage

### Parsing a Streams File

```cpp
#include "cece/cece_tide_yaml_serializer.hpp"

// Parse streams file
cece::CeceDataConfig config =
    cece::SerializeTideYaml("cece_emissions.yaml");

// Access parsed streams
for (const auto& stream : config.cece_data.streams) {
    std::cout << "Stream: " << stream.name << std::endl;
    std::cout << "  Files: " << stream.file_paths.size() << std::endl;
    std::cout << "  Variables: " << stream.variables.size() << std::endl;
}
```

### Validating a Configuration

```cpp
#include "cece/cece_config_validator.hpp"

cece::CeceDataConfig config = /* ... */;

std::vector<std::string> errors;
bool is_valid = cece::ConfigValidator::ValidateTIDE(config, errors);

if (!is_valid) {
    std::cerr << "Configuration validation failed:" << std::endl;
    for (const auto& error : errors) {
        std::cerr << "  - " << error << std::endl;
    }
}
```

### Writing a Streams File

```cpp
#include "cece/cece_tide_yaml_serializer.hpp"

// Create configuration programmatically
cece::CeceDataConfig config;
cece::CeceDataStreamConfig stream;

stream.name = "test_emissions";
stream.file_paths = {"/data/emissions.nc"};
stream.variables = {{"SO2", "so2_flux"}};

config.cece_data.streams.push_back(stream);

// Write to file
cece::SerializeTideYaml(config, "output.yaml");
```

```cpp
#include "cece/cece_tide_yaml_serializer.hpp"

// Create configuration programmatically
cece::CeceDataConfig config;
cece::CeceDataStreamConfig stream;
stream.name = "test_emissions";
stream.file_paths = {"/data/emissions.nc"};
stream.variables = {{"CO_emis", "CO"}, {"NOx_emis", "NOx"}};
stream.taxmode = "cycle";
stream.tintalgo = "linear";
config.cece_data.streams.push_back(stream);

// Write to file
cece::SerializeTideYaml(config, "output.yaml");
```

## Validation

The parser performs comprehensive validation:

### File Path Validation

- Checks that all file paths exist
- Verifies files are readable
- Validates files are valid NetCDF format

### Variable Validation

- Checks that all specified variables exist in NetCDF files
- Reports missing variables with file path context

### Interpolation Mode Validation

- Validates temporal interpolation mode is one of: none, linear, nearest, lower, upper
- Validates time axis mode is one of: cycle, extend, limit
- Provides list of valid options in error messages

### Error Messages

All validation errors include:
- Stream name for context
- Specific problem description
- Suggested corrective action

Example error messages:
```
Stream 'anthro_emissions': File not found: /data/missing.nc
Stream 'anthro_emissions': Variable 'CO_emis' not found in file: /data/emissions.nc
Stream 'anthro_emissions': Invalid temporal interpolation mode 'bad_mode'. Valid options: none, linear, nearest, lower, upper
```

## Round-Trip Property

The parser supports round-trip serialization (Property 16):

```cpp
// Parse original file
auto config1 = cece::ParseTideYaml("original.yaml");

// Write to new file
cece::SerializeTideYaml(config1, "copy.yaml");

// Parse the copy
auto config2 = cece::ParseTideYaml("copy.yaml");

// config1 and config2 should be equivalent
```

This property ensures that:
- No information is lost during serialization
- Configurations can be programmatically modified and saved
- Testing can verify parser correctness

## Integration with CECE

The parser integrates with CECE configuration:

```cpp
// In cece_config.hpp
struct CeceConfig {
    // ... other fields ...
    CeceDataConfig cece_data;  // Parsed streams configuration
};

// In cece_data_ingestor.cpp
void CeceDataIngestor::IngestEmissionsInline(
    const CeceDataConfig& config,
    CeceImportState& cece_state,
    int nx, int ny, int nz) {
    // Use parsed configuration to ingest emissions from TIDE
}
```

## Examples

See `examples/cece_config_ex*.yaml` for complete examples demonstrating:
- Anthropogenic emissions from CEDS
- Biogenic emissions from MEGAN
- Biomass burning emissions from GFED
- Meteorological fields
- Scale factors for temporal variation

## Testing

Unit tests are provided in `tests/cf_ingestor/test_tide_yaml_serializer.cpp`:

```bash
# Build and run tests
mkdir build && cd build
cmake ..
make test_tide_yaml_serializer
./test_tide_yaml_serializer
```

Tests cover:
- Parsing valid YAML streams files
- Handling missing files
- Validating interpolation modes
- Detecting missing attributes
- Round-trip serialization
- Variables without explicit mapping
- Comments and whitespace handling
- Empty configurations
- All optional attributes

## Requirements Satisfied

This implementation satisfies the following requirements:

- **Requirement 4.1**: TIDE YAML Configuration
- **Requirement 4.3**: TIDE YAML Serialization
- **Requirement 4.4**: TIDE YAML Round-Trip
- **Requirement 4.6**: TIDE YAML Error Handling
  - 7.1: Parse ESMF Config format
  - 7.2: Validate required attributes
  - 7.3: Validate file paths
  - 7.4: Check file readability
  - 7.5: Validate variables exist
  - 7.6: Validate interpolation modes
  - 7.7: Report line numbers for syntax errors
  - 7.8: Report missing file paths
  - 7.9: Report missing variables
  - 7.10: List valid options for invalid modes
- **Requirement 7.13**: Round-trip serialization
- **Property 16**: Streams Configuration Round-Trip

## Future Enhancements

Potential future enhancements:
- Support for YAML format as alternative to ESMF Config
- Schema validation using JSON Schema or similar
- Integration with TIDE validation tools
- Support for stream templates and includes
- Automatic mesh file discovery
- Variable dimension validation
