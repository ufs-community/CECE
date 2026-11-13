# Point Stack Format (`format: point_nc`)

This document describes the required format for point source emissions datasets configured with `format: point_nc` (using `kind: point`).

## Specification: CF-1.13 Unified Point Source Emissions NetCDF Format

This document defines a standard NetCDF format for stationary point source emissions. It consolidates the two traditional Models-3 IOAPI files used in CMAQ modeling (`STACK_GROUPS` and `INLN`) into a single, fully CF-1.13 compliant dataset using the **Orthogonal Multidimensional Array Representation of Time Series** (CF Conventions, Section 9.3.2 and Appendix H.2.1).

## 1. Storage Model & Format Requirements

* **File Format**: NetCDF-4 (`NETCDF4` / HDF5-based).

* **String Encoding**: NetCDF-4 variable-length strings (`NC_STRING` / `string`) must be used for all identifiers and labels to prevent arbitrary character-length truncation.

* **Coordinate System**: Unprojected geodetic coordinates (WGS-84 datum) using geodetic latitude and longitude. No projected grid mappings (`grid_mapping`, projection coordinates $x$, $y$) are required.

## 2. Global Attributes

| Attribute Name | Requirement | Type | Permitted / Required Value | Description |
| ----- | ----- | ----- | ----- | ----- |
| `Conventions` | **Required** | `string` | `"CF-1.13"` | Identifies the file as conforming to CF Version 1.13. |
| `featureType` | **Required** | `string` | `"timeSeries"` | Identifies the Discrete Sampling Geometry (DSG) type. |
| `title` | Optional | `string` | User-defined | Short dataset descriptive title. |
| `source` | Optional | `string` | User-defined | Upstream model, inventory, or tool used to create the file. |
| `history` | Optional | `string` | User-defined | Audit trail of file creation or modifications. |

## 3. Dimensions

| Dimension Name | Requirement | Dimension Policy | Size / Bound | Description |
| ----- | ----- | ----- | ----- | ----- |
| `stack` | **Required** | Fixed Name | Fixed integer ($N_{\text{stacks}} \ge 1$) | Number of individual point sources / stacks. |
| `time` | **Required** | Fixed Name | Fixed integer or `UNLIMITED` ($N_{\text{time}} \ge 1$) | Synchronized temporal emission steps (e.g., hourly). |

## 4. Master Variable Specification Matrix

| Variable Name | Requirement | Name Policy | Data Type | Dimensions | Unit Policy | Allowed / Required Units | Attributes Required |
| ----- | ----- | ----- | ----- | ----- | ----- | ----- | ----- |
| `time` | **Required** | **Fixed** | `double` (`f8`) | `(time)` | User-defined epoch | `<time-unit> since <epoch>` | `standard_name`, `units`, `calendar`, `axis` |
| `latitude` | **Required** | **Fixed** | `float` (`f4`) | `(stack)` | **Fixed** | `"degrees_north"` | `standard_name`, `units`, `axis` |
| `longitude` | **Required** | **Fixed** | `float` (`f4`) | `(stack)` | **Fixed** | `"degrees_east"` | `standard_name`, `units`, `axis` |
| `stack_id` | Optional | **Fixed** | `string` | `(stack)` | None | None | `cf_role = "timeseries_id"`, `long_name` |
| `facility_id` | Optional | **Fixed** | `string` | `(stack)` | None | None | `long_name` |
| `unit_id` | Optional | **Fixed** | `string` | `(stack)` | None | None | `long_name` |
| `scc` | Optional | **Fixed** | `string` | `(stack)` | None | None | `long_name` |
| `STKHT` | **Required** | **Fixed** | `float` (`f4`) | `(stack)` | **Fixed** | `"m"` | `units`, `coordinates`, `_FillValue` |
| `STKDM` | **Required** | **Fixed** | `float` (`f4`) | `(stack)` | **Fixed** | `"m"` | `units`, `coordinates`, `_FillValue` |
| `STKTK` | **Required** | **Fixed** | `float` (`f4`) | `(stack)` | **Fixed** | `"K"` | `units`, `coordinates`, `_FillValue` |
| `STKVE` | **Required** | **Fixed** | `float` (`f4`) | `(stack)` | **Fixed** | `"m s-1"` | `units`, `coordinates`, `_FillValue` |
| `STKFLW` | **Required** | **Fixed** | `float` (`f4`) | `(stack)` | **Fixed** | `"m3 s-1"` | `units`, `coordinates`, `_FillValue` |
| *`<SPECIES_NAME>`* | **Required** (at least 1) | **User-Selected** | `float` (`f4`) | `(time, stack)` | **User-Selected** | Any valid UDUNITS rate (e.g., `mol m-2 s-1`, `g m-2 s-1`) | `units`, `coordinates`, `_FillValue` |

## 5. Detailed Variable Definitions

### 5.1 Coordinate Variables

#### `time`

* **Status**: **Required**

* **Variable Name**: Fixed to `time`

* **Data Type**: `double` (64-bit IEEE float)

* **Dimensions**: `(time)`

* **Description**: Synchronized time coordinate for all point sources.

* **Units Policy**: User-selected epoch conforming to UDUNITS standards (e.g., `"hours since 2026-07-01 00:00:00"`, `"seconds since 1970-01-01 00:00:00"`).

* **Mandatory Attributes**:

  * `standard_name`: `"time"`

  * `units`: Valid CF time specification

  * `calendar`: Standard calendar identifier (e.g., `"standard"`, `"gregorian"`, or `"proleptic_gregorian"`)

  * `axis`: `"T"`

#### `latitude`

* **Status**: **Required**

* **Variable Name**: Fixed to `latitude`

* **Data Type**: `float` (32-bit IEEE float)

* **Dimensions**: `(stack)`

* **Description**: Geodetic latitude of the stack center.

* **Units Policy**: **Fixed** to `"degrees_north"`

* **Mandatory Attributes**:

  * `standard_name`: `"latitude"`

  * `units`: `"degrees_north"`

  * `axis`: `"Y"`

#### `longitude`

* **Status**: **Required**

* **Variable Name**: Fixed to `longitude`

* **Data Type**: `float` (32-bit IEEE float)

* **Dimensions**: `(stack)`

* **Description**: Geodetic longitude of the stack center.

* **Units Policy**: **Fixed** to `"degrees_east"`

* **Mandatory Attributes**:

  * `standard_name`: `"longitude"`

  * `units`: `"degrees_east"`

  * `axis`: `"X"`

### 5.2 Identification and Auxiliary Variables

#### `stack_id`

* **Status**: **Optional** (Note: CF-1.13 DSG generally expects a variable with `cf_role = "timeseries_id"`, but it is not strictly required by this specific format schema).

* **Variable Name**: Fixed to `stack_id`

* **Data Type**: `string` (NetCDF-4 variable-length string)

* **Dimensions**: `(stack)`

* **Description**: Unique identifier assigned to each stack or emission point.

* **Units**: None

* **Mandatory Attributes**:

  * `cf_role`: **Fixed** to `"timeseries_id"`

  * `long_name`: Descriptive label (e.g., `"Unique stack identifier"`)

#### Optional Auxiliary Identifiers (`facility_id`, `unit_id`, `scc`)

* **Status**: **Optional** (ignored by downstream simulation engines)

* **Variable Names**: Recommended fixed conventions:

  * `facility_id`: Parent facility or plant identifier.

  * `unit_id`: Source unit, boiler, or process identifier.

  * `scc`: EPA Source Classification Code.

* **Data Type**: `string` (variable-length string)

* **Dimensions**: `(stack)`

* **Units**: None

* **Mandatory Attributes**:

  * `long_name`: Description of identifier contents

### 5.3 Static Stack Parameters (CMAQ IOAPI Equivalents)

All parameters in this group must remain time-invariant and use exact CMAQ variable names and units.

#### `STKHT`

* **Status**: **Required**

* **Variable Name**: Fixed to `STKHT`

* **Data Type**: `float`

* **Dimensions**: `(stack)`

* **Description**: Physical stack height above surface ground level.

* **Units Policy**: **Fixed** to `"m"`

* **Mandatory Attributes**:

  * `standard_name`: `height`

  * `units`: `"m"`

  * `coordinates`: `"latitude longitude"`

  * `_FillValue`: Value denoting missing data (standard: `-999.0f`)

#### `STKDM`

* **Status**: **Required**

* **Variable Name**: Fixed to `STKDM`

* **Data Type**: `float`

* **Dimensions**: `(stack)`

* **Description**: Inside stack diameter at exit point.

* **Units Policy**: **Fixed** to `"m"`

* **Mandatory Attributes**:

  * `units`: `"m"`

  * `coordinates`: `"latitude longitude"`

  * `_FillValue`: Standard: `-999.0f`

#### `STKTK`

* **Status**: **Required**

* **Variable Name**: Fixed to `STKTK`

* **Data Type**: `float`

* **Dimensions**: `(stack)`

* **Description**: Exhaust gas temperature at stack exit.

* **Units Policy**: **Fixed** to `"K"`

* **Mandatory Attributes**:

  * `units`: `"K"`

  * `coordinates`: `"latitude longitude"`

  * `_FillValue`: Standard: `-999.0f`

#### `STKVE`

* **Status**: **Required**

* **Variable Name**: Fixed to `STKVE`

* **Data Type**: `float`

* **Dimensions**: `(stack)`

* **Description**: Exhaust gas velocity at stack exit.

* **Units Policy**: **Fixed** to `"m s-1"`

* **Mandatory Attributes**:

  * `units`: `"m s-1"`

  * `coordinates`: `"latitude longitude"`

  * `_FillValue`: Standard: `-999.0f`

#### `STKFLW`

* **Status**: **Required**

* **Variable Name**: Fixed to `STKFLW`

* **Data Type**: `float`

* **Dimensions**: `(stack)`

* **Description**: Exhaust gas volumetric flow rate.

* **Units Policy**: **Fixed** to `"m3 s-1"`

* **Formula**:


  $$
  \text{STKFLW} = \text{STKVE} \times \pi \left(\frac{\text{STKDM}}{2}\right)^2
  $$

* **Mandatory Attributes**:

  * `units`: `"m3 s-1"`

  * `coordinates`: `"latitude longitude"`

  * `_FillValue`: Standard: `-999.0f`

### 5.4 Speciated Emissions Variables

At least one emission species variable must be present in the dataset.

* **Status**: **Required** (minimum 1 species variable)

* **Variable Name**: **User-Selected** (e.g., `NO`, `NO2`, `SO2`, `CO`, `NH3`, `FORM`, `ISOP`, `PM25_TOT`)

* **Data Type**: `float` (32-bit IEEE float)

* **Dimensions**: Must be 2D: `(time, stack)`

* **Units Policy**: **User-Selected**; must specify a valid rate string conforming to UDUNITS (e.g., `"mol s-1"`, `"g s-1"`, `"kg h-1"`, `"tons d-1"`).

* **Mandatory Attributes**:

  * `units`: Explicit string of the user-chosen unit.

  * `coordinates`: Must be set exactly to `"time latitude longitude"`.

  * `_FillValue`: Recommended `-999.0f` or `1.0e36f`.

* **Optional Attributes**:

  * `long_name`: Plain-language species description.

## 6. Complete Reference CDL Architecture

Below is the formal Network Common Data form Language (CDL) template representing a fully populated instance of this format:

```
netcdf point_source_emissions_cf113 {
dimensions:
    stack = 3 ;
    time = UNLIMITED ; // (24 currently)

variables:
    // =========================================================================
    // Coordinate Variables (Fixed Names, Fixed Attributes)
    // =========================================================================
    double time(time) ;
        time:standard_name = "time" ;
        time:long_name = "Emission valid time" ;
        time:units = "hours since 2026-07-01 00:00:00" ;
        time:calendar = "standard" ;
        time:axis = "T" ;

    float latitude(stack) ;
        latitude:standard_name = "latitude" ;
        latitude:long_name = "Stack geodetic latitude" ;
        latitude:units = "degrees_north" ;
        latitude:axis = "Y" ;

    float longitude(stack) ;
        longitude:standard_name = "longitude" ;
        longitude:long_name = "Stack geodetic longitude" ;
        longitude:units = "degrees_east" ;
        longitude:axis = "X" ;

    // =========================================================================
    // Identifiers (NetCDF-4 variable-length string types)
    // =========================================================================
    string stack_id(stack) ; // Optional
        stack_id:cf_role = "timeseries_id" ;
        stack_id:long_name = "Unique stack identifier" ;

    string facility_id(stack) ; // Optional
        facility_id:long_name = "Facility identifier" ;

    string unit_id(stack) ;     // Optional
        unit_id:long_name = "Process unit identifier" ;

    string scc(stack) ;         // Optional
        scc:long_name = "Source Classification Code" ;

    // =========================================================================
    // Static Stack Parameters (Fixed Names, Fixed Units)
    // =========================================================================
    float STKHT(stack) ;
        STKHT:long_name = "Physical stack height above ground level" ;
        STKHT:standard_name = "height" ;
        STKHT:units = "m" ;
        STKHT:coordinates = "latitude longitude" ;
        STKHT:_FillValue = -999.0f ;

    float STKDM(stack) ;
        STKDM:long_name = "Inside stack diameter" ;
        STKDM:units = "m" ;
        STKDM:coordinates = "latitude longitude" ;
        STKDM:_FillValue = -999.0f ;

    float STKTK(stack) ;
        STKTK:long_name = "Stack exit gas temperature" ;
        STKTK:units = "K" ;
        STKTK:coordinates = "latitude longitude" ;
        STKTK:_FillValue = -999.0f ;

    float STKVE(stack) ;
        STKVE:long_name = "Stack exit gas velocity" ;
        STKVE:units = "m s-1" ;
        STKVE:coordinates = "latitude longitude" ;
        STKVE:_FillValue = -999.0f ;

    float STKFLW(stack) ;
        STKFLW:long_name = "Stack exit gas flow rate" ;
        STKFLW:units = "m3 s-1" ;
        STKFLW:coordinates = "latitude longitude" ;
        STKFLW:_FillValue = -999.0f ;

    // =========================================================================
    // Speciated Emissions (User-Selected Names, User-Selected Units)
    // =========================================================================
    float NO(time, stack) ;
        NO:long_name = "Nitric oxide emission rate" ;
        NO:units = "mol s-1" ;
        NO:coordinates = "time latitude longitude" ;
        NO:_FillValue = -999.0f ;

    float SO2(time, stack) ;
        SO2:long_name = "Sulfur dioxide emission rate" ;
        SO2:units = "mol s-1" ;
        SO2:coordinates = "time latitude longitude" ;
        SO2:_FillValue = -999.0f ;

    float PM25(time, stack) ;
        PM25:long_name = "Particulate matter <= 2.5um emission rate" ;
        PM25:units = "g s-1" ;
        PM25:coordinates = "time latitude longitude" ;
        PM25:_FillValue = -999.0f ;

// =============================================================================
// Global Attributes
// =============================================================================
// global attributes:
    :Conventions = "CF-1.13" ;
    :featureType = "timeSeries" ;
    :title = "Unified Point Source Emissions Inventory" ;
    :source = "CMAQ/SMOKE CF-1.13 Formatter" ;
}

```

## 7. Implementation Checklist for Encoders & Decoders

- [ ] **Engine Support**: The file writer must set the file format to NetCDF-4 (`NETCDF4`). Classic model formats (`NETCDF3_CLASSIC` or `NETCDF3_64BIT_OFFSET`) cannot be used because they do not support `NC_STRING`.

- [ ] **Dimensional Consistency**: All emissions variables must possess dimensions in the exact order `(time, stack)`. Static stack parameter variables must have dimension `(stack)`.

- [ ] **CF Coordinates Attribute**:

  * Any variable with dimensions `(stack)` must declare `coordinates = "latitude longitude"`.

  * Any variable with dimensions `(time, stack)` must declare `coordinates = "time latitude longitude"`.

- [ ] **No Spatial Projection Variables Needed**: Writers must not include `grid_mapping` attributes or Cartesian coordinates ($x$, $y$) unless downstream software explicitly requests them. Latitudes and longitudes alone fulfill CF-1.13 spatial requirements.
