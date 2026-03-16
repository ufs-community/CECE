# CDEPS Streams Configuration Parser

## Overview

The CDEPS Streams Configuration Parser provides functionality to parse, validate, and serialize ESMF Config format streams files used by CDEPS-inline for data ingestion in ACES. This enables hybrid data ingestion where static emission inventories from NetCDF files are combined with live meteorological fields from ESMF.

## Features

- **ESMF Config Format Parsing**: Reads streams files in ESMF Config format
- **Comprehensive Validation**: Validates file paths, variables, and interpolation modes
- **Round-Trip Serialization**: Supports writing configurations back to ESMF Config format
- **Detailed Error Messages**: Provides actionable error messages for configuration issues
- **NetCDF Integration**: Validates that variables exist in specified NetCDF files

## ESMF Config Format

The ESMF Config format for CDEPS streams uses a simple key-value syntax:

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
#include "aces/aces_cdeps_parser.hpp"

// Parse streams file
aces::AcesCdepsConfig config =
    aces::CdepsStreamsParser::ParseStreamsFile("aces_emissions.streams");

// Access parsed streams
for (const auto& stream : config.streams) {
    std::cout << "Stream: " << stream.name << std::endl;
    std::cout << "  Files: " << stream.file_paths.size() << std::endl;
    std::cout << "  Variables: " << stream.variables.size() << std::endl;
}
```

### Validating a Configuration

```cpp
#include "aces/aces_cdeps_parser.hpp"

aces::AcesCdepsConfig config =
    aces::CdepsStreamsParser::ParseStreamsFile("aces_emissions.streams");

std::vector<std::string> errors;
bool is_valid = aces::CdepsStreamsParser::ValidateStreamsConfig(config, errors);

if (!is_valid) {
    std::cerr << "Configuration validation failed:" << std::endl;
    for (const auto& error : errors) {
        std::cerr << "  - " << error << std::endl;
    }
}
```

### Writing a Streams File

```cpp
#include "aces/aces_cdeps_parser.hpp"

// Create configuration programmatically
aces::AcesCdepsConfig config;
aces::CdepsStreamConfig stream;
stream.name = "test_emissions";
stream.file_paths = {"/data/emissions.nc"};
stream.variables = {{"CO_emis", "CO"}, {"NOx_emis", "NOx"}};
stream.taxmode = "cycle";
stream.tintalgo = "linear";
config.streams.push_back(stream);

// Write to file
aces::CdepsStreamsParser::WriteStreamsFile("output.streams", config);
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
auto config1 = aces::CdepsStreamsParser::ParseStreamsFile("original.streams");

// Write to new file
aces::CdepsStreamsParser::WriteStreamsFile("copy.streams", config1);

// Parse the copy
auto config2 = aces::CdepsStreamsParser::ParseStreamsFile("copy.streams");

// config1 and config2 should be equivalent
```

This property ensures that:
- No information is lost during serialization
- Configurations can be programmatically modified and saved
- Testing can verify parser correctness

## Integration with ACES

The parser integrates with ACES configuration:

```cpp
// In aces_config.hpp
struct AcesConfig {
    // ... other fields ...
    AcesCdepsConfig cdeps_config;  // Parsed streams configuration
};

// In aces_data_ingestor.cpp
void AcesDataIngestor::InitializeCDEPS(
    ESMC_GridComp gcomp,
    ESMC_Clock clock,
    ESMC_State exportState,
    const AcesCdepsConfig& config) {
    // Use parsed configuration to initialize CDEPS
}
```

## Examples

See `examples/cdeps_streams_example.txt` for complete examples demonstrating:
- Anthropogenic emissions from CEDS
- Biogenic emissions from MEGAN
- Biomass burning emissions from GFED
- Meteorological fields
- Scale factors for temporal variation

## Testing

Unit tests are provided in `tests/test_cdeps_parser.cpp`:

```bash
# Build and run tests
mkdir build && cd build
cmake ..
make test_cdeps_parser
./test_cdeps_parser
```

Tests cover:
- Parsing valid streams files
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

- **Requirement 1.1**: CDEPS-Inline Hybrid Data Ingestion
- **Requirement 1.2**: CDEPS configuration validation
- **Requirement 7.1-7.10**: CDEPS Streams Configuration Parser
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
- Integration with CDEPS validation tools
- Support for stream templates and includes
- Automatic mesh file discovery
- Variable dimension validation
