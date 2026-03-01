# HEMCO Migration Examples

This page provides a visual comparison between classic HEMCO configuration (`HEMCO_Config.rc`) and the corresponding ACES configuration (`aces_config.yaml`).

ACES maintains algorithmic parity with HEMCO's stacking engine while utilizing a modern, performance-portable YAML format.

---

## Example 1: Add global anthropogenic emissions
**Scenario:** Monthly global anthropogenic CO emissions with gridded hourly scale factors.

### [Before] HEMCO
```fortran
# ExtNr Name       sourceFile     sourceVar sourceTime          CRE Dim Unit     Species ScalID Cat Hier
0      MACCITY_CO  $ROOT/MACCity.nc  CO     1980-2014/1-12/1/0  C   xy  kg/m2/s  CO      500    1   1

# ScalID Name            srcFile         srcVar  srcTime          CRE Dim Unit Oper
500      HOURLY_SCALFACT $ROOT/hourly.nc factor  2000/1/1/0-23    C   xy  1    1
```

### [After] ACES
```yaml
scale_factors:
  hourly_scalfact: HOURLY_SCALFACT

species:
  co:
    - field: "MACCITY_CO"
      operation: "add"
      scale: 1.0
      scale_fields: ["hourly_scalfact"]

cdeps_inline_config:
  streams:
    - name: "MACCITY_CO"
      file: "data/MACCity_4x5.nc"
    - name: "HOURLY_SCALFACT"
      file: "data/hourly.nc"
```

---

## Example 2: Overlay regional emissions
**Scenario:** Regional European CO emissions replace global emissions in a specific region.

### [Before] HEMCO
```fortran
#ExtNr Name       srcFile          srcVar srcTime             CRE Dim Unit     Species ScalIDs  Cat Hier
0      MACCITY_CO $ROOT/MACCity.nc  CO    1980-2014/1-12/1/0  C   xy  kg/m2/s  CO      500      1   1
0      EMEP_CO    $ROOT/EMEP.nc     CO    2000-2014/1-12/1/0  C   xy  kg/m2/s  CO      500/1001 1   2

#ScalID Name         srcFile             srcVar srcTime      CRE Dim Unit Oper Box
1001    MASK_EUROPE  $ROOT/mask_europe.nc MASK  2000/1/1/0   C   xy  1    1    -30/30/45/70
```

### [After] ACES
```yaml
scale_factors:
  hourly_scalfact: HOURLY_SCALFACT

masks:
  mask_europe: MASK_EUROPE

species:
  co:
    - field: "MACCITY_CO"
      category: "anthropogenic"
      hierarchy: 1
      operation: "add"
      scale_fields: ["hourly_scalfact"]
    - field: "EMEP_CO"
      category: "anthropogenic"
      hierarchy: 2
      operation: "replace"
      mask: "mask_europe"
      scale_fields: ["hourly_scalfact"]
```

---

## Example 3: Adding aircraft emissions
**Scenario:** Adding AEIC aircraft emissions into a separate emission category.

### [Before] HEMCO
```fortran
#ExtNr Name       srcFile          srcVar srcTime             CRE Dim Unit     Species ScalIDs  Cat Hier
0      MACCITY_CO $ROOT/MACCity.nc  CO    1980-2014/1-12/1/0  C   xy  kg/m2/s  CO      500      1   1
0      AEIC_CO    $ROOT/AEIC.nc     CO    2005/1-12/1/0       C   xyz kg/m2/s  CO      -        2   1
```

### [After] ACES
```yaml
species:
  co:
    - field: "MACCITY_CO"
      category: "anthropogenic"
    - field: "AEIC_CO"
      category: "aircraft"
```

---

## Example 4: Add biomass burning emissions
**Scenario:** Using the GFED4 extension for biomass burning.

### [Before] HEMCO
```fortran
# ExtNr ExtName  on/off  Species
111     GFED     : on    CO
    --> GFED4    :       true

#ExtNr Name      srcFile          srcVar srcTime             CRE Dim Unit     Species ScalIDs  Cat Hier
111    GFED_WDL  $ROOT/GFED4.nc   WDL_DM 2000-2013/1-12/01/0 C   xy  kg/m2/s  *       -        1   1
```

### [After] ACES
```yaml
physics_schemes:
  - name: "GFED"
    language: "cpp"
    options:
      version: "GFED4"

species:
  co:
    - field: "MACCITY_CO"
      operation: "add"
```

---

## Example 5: Additional species
**Scenario:** Adding NO and SO2 to the configuration.

### [Before] HEMCO
```fortran
0 MACCITY_CO  $ROOT/MACCity.nc CO  1980-2014/1-12/1/0 C xy  kg/m2/s CO  500 1 1
0 MACCITY_NO  $ROOT/MACCity.nc NO  1980-2014/1-12/1/0 C xy  kg/m2/s NO  500 1 1
0 MACCITY_SO2 $ROOT/MACCity.nc SO2 1980-2014/1-12/1/0 C xy  kg/m2/s SO2 -   1 1
```

### [After] ACES
```yaml
species:
  co:
    - field: "MACCITY_CO"
  no:
    - field: "MACCITY_NO"
  so2:
    - field: "MACCITY_SO2"
```

---

## Example 6: Non-separated inventories
**Scenario:** Inventories (like EDGAR/CEDS) that lump biofuels/trash with anthropogenic.

### [Before] HEMCO
```fortran
# EDGAR NO emissions assigned to Category 1 (Anthro), but effectively covers Cat 2 (Biofuel)
0 EDGAR_NO_POW EDGAR.nc emi_nox 1970-2010/1/1/0 C xy kg/m2/s NO 1201/25/115 1/2 2
```

### [After] ACES
```yaml
species:
  no:
    - field: "EDGAR_NO_POW"
      category: "anthropogenic"
      operation: "add"
```
