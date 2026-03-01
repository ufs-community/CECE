#!/usr/bin/env python3
import yaml
import argparse
import os

class HemcoParser:
    def __init__(self, hemco_config_path):
        self.config_path = hemco_config_path
        self.base_dir = os.path.dirname(hemco_config_path)
        self.settings = {}
        self.base_emissions = []
        self.scale_factors = {}
        self.masks = {}
        self.extensions = {}
        self.parse()

    def strip_comments(self, line):
        if '#' in line:
            return line[:line.find('#')].strip()
        return line.strip()

    def parse(self):
        if not os.path.exists(self.config_path):
            print(f"Warning: HEMCO config file {self.config_path} not found.")
            return

        with open(self.config_path, 'r') as f:
            lines = f.readlines()

        current_section = None
        for line in lines:
            raw_line = line.strip()
            line = self.strip_comments(raw_line)

            if not line:
                if 'BEGIN SECTION SETTINGS' in raw_line:
                    current_section = 'SETTINGS'
                elif 'BEGIN SECTION EXTENSION SWITCHES' in raw_line:
                    current_section = 'EXTENSIONS'
                elif 'BEGIN SECTION BASE EMISSIONS' in raw_line:
                    current_section = 'BASE_EMISSIONS'
                elif 'BEGIN SECTION SCALE FACTORS' in raw_line:
                    current_section = 'SCALE_FACTORS'
                elif 'BEGIN SECTION MASKS' in raw_line:
                    current_section = 'MASKS'
                elif 'END SECTION' in raw_line:
                    current_section = None
                continue

            if current_section == 'SETTINGS':
                if ':' in line:
                    key, value = line.split(':', 1)
                    self.settings[key.strip()] = value.strip()

            elif current_section == 'EXTENSIONS':
                # Status might have a colon if it's "--> NAME : status"
                line = line.replace('-->', '').strip()
                if ':' in line:
                    parts = line.split(':')
                    name_part = parts[0].strip().split()
                    if not name_part: continue
                    ext_name = name_part[-1]
                    status = parts[1].strip().split()[0] if parts[1].strip() else "off"
                    self.extensions[ext_name] = {'status': status}
                else:
                    parts = line.split()
                    if len(parts) >= 3:
                        ext_nr = parts[0]
                        ext_name = parts[1]
                        status = parts[2].strip(':')
                        self.extensions[ext_name] = {'nr': ext_nr, 'status': status}

            elif current_section == 'BASE_EMISSIONS':
                parts = line.split()
                if len(parts) >= 12:
                    entry = {
                        'ext_nr': parts[0],
                        'name': parts[1],
                        'file': parts[2],
                        'var': parts[3],
                        'time': parts[4],
                        'cre': parts[5],
                        'dim': parts[6],
                        'unit': parts[7],
                        'species': parts[8],
                        'scal_ids': parts[9],
                        'cat': parts[10],
                        'hier': parts[11]
                    }
                    self.base_emissions.append(entry)

            elif current_section == 'SCALE_FACTORS':
                parts = line.split()
                if len(parts) >= 9:
                    scal_id = parts[0]
                    entry = {
                        'name': parts[1],
                        'file': parts[2],
                        'var': parts[3],
                        'time': parts[4],
                        'cre': parts[5],
                        'dim': parts[6],
                        'unit': parts[7],
                        'oper': parts[8]
                    }
                    self.scale_factors[scal_id] = entry

            elif current_section == 'MASKS':
                parts = line.split()
                if len(parts) >= 9:
                    scal_id = parts[0]
                    entry = {
                        'name': parts[1],
                        'file': parts[2],
                        'var': parts[3],
                        'time': parts[4],
                        'cre': parts[5],
                        'dim': parts[6],
                        'unit': parts[7],
                        'oper': parts[8]
                    }
                    if len(parts) > 9:
                        entry['box'] = parts[9]
                    self.masks[scal_id] = entry

class DiagnParser:
    def __init__(self, diagn_config_path):
        self.config_path = diagn_config_path
        self.variables = []
        self.parse()

    def parse(self):
        if not os.path.exists(self.config_path):
            return
        with open(self.config_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = line.split()
                if len(parts) >= 1:
                    self.variables.append(parts[0])

class GridParser:
    def __init__(self, grid_config_path):
        self.config_path = grid_config_path
        self.grid_params = {}
        self.parse()

    def parse(self):
        if not os.path.exists(self.config_path):
            return
        with open(self.config_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if ':' in line:
                    key, value = line.split(':', 1)
                    self.grid_params[key.strip().upper()] = value.strip()

def convert_hemco_to_aces(hemco_config_path, output_path):
    parser = HemcoParser(hemco_config_path)
    base_dir = parser.base_dir

    aces_config = {
        'meteorology': {},
        'temporal_cycles': {},
        'species': {},
        'cdeps_inline_config': {'streams': []}
    }

    # Identify enabled extensions
    enabled_ext_nrs = {'0'} # Base is always enabled
    for name, info in parser.extensions.items():
        if info.get('status') in ['on', 'true', 'yes']:
            if 'nr' in info:
                enabled_ext_nrs.add(info['nr'])
            # Some extensions might be sub-items of Base (ExtNr 0)
            # HEMCO uses brackets or names to enable them.
            # For simplicity, we'll assume they are handled by checking info['status']

    # Map Base Emissions to Species Layers
    streams = {}

    for be in parser.base_emissions:
        # Identify associated extension name for this entry if it's within a collection bracket
        # (For now, just check if it's assigned to an extension in the switcher)

        # Filter by enabled extensions
        # ext_nr = 0 is Base.
        # If it's another number, check if that number is enabled.
        if be['ext_nr'] != '0' and be['ext_nr'] not in enabled_ext_nrs:
            # Check if there is an extension name that matches and is on/off
            # HEMCO also allows enabling by name in the switches
            is_enabled = False
            for ext_name, info in parser.extensions.items():
                if info.get('nr') == be['ext_nr'] and info.get('status') in ['on', 'true', 'yes']:
                    is_enabled = True
                    break
            if not is_enabled:
                continue

        species_name = be['species'].lower()
        if species_name == '*': continue # Skip wildcard species for now

        if species_name not in aces_config['species']:
            aces_config['species'][species_name] = []

        layer = {
            'field': be['name'],
            'operation': 'add' if be['hier'] == '1' else 'replace',
            'scale': 1.0,
            'category': be['cat'],
            'hierarchy': int(be['hier'])
        }

        # Handle Scale IDs (Scale Factors and Masks)
        scal_ids = be['scal_ids'].split('/')
        layer_masks = []
        layer_scale_fields = []

        for sid in scal_ids:
            if sid == '-': continue
            if sid in parser.masks:
                mask_entry = parser.masks[sid]
                mask_name = mask_entry['name']
                layer_masks.append(mask_name.lower())
                if mask_entry['file'] != '-':
                    streams[mask_name] = mask_entry['file']
            elif sid in parser.scale_factors:
                sf_entry = parser.scale_factors[sid]
                sf_name = sf_entry['name']

                # Check if it's a temporal cycle (constant values in file column)
                if '/' in sf_entry['file'] and sf_entry['var'] == '-':
                    try:
                        values = [float(x) for x in sf_entry['file'].split('/')]
                        cycle_name = sf_name.lower()
                        if len(values) == 24:
                            aces_config['temporal_cycles'][cycle_name] = values
                            layer['diurnal_cycle'] = cycle_name
                        elif len(values) == 7:
                            aces_config['temporal_cycles'][cycle_name] = values
                            layer['weekly_cycle'] = cycle_name
                        else:
                            aces_config['temporal_cycles'][cycle_name] = values
                    except ValueError:
                        # Not a list of floats, probably a file path with slashes
                        layer_scale_fields.append(sf_name.lower())
                        if sf_entry['file'] != '-':
                            streams[sf_name] = sf_entry['file']
                else:
                    layer_scale_fields.append(sf_name.lower())
                    if sf_entry['file'] != '-':
                        streams[sf_name] = sf_entry['file']

        if layer_masks:
            layer['mask'] = layer_masks if len(layer_masks) > 1 else layer_masks[0]
        if layer_scale_fields:
            layer['scale_fields'] = layer_scale_fields

        aces_config['species'][species_name].append(layer)

        if be['file'] != '-':
            streams[be['name']] = be['file']

    # Build CDEPS streams
    for name, file in streams.items():
        clean_file = file.replace('$ROOT/', 'data/').replace('$ROOT', 'data')
        aces_config['cdeps_inline_config']['streams'].append({
            'name': name,
            'file': clean_file
        })

    # Meteorology mapping
    for sf_id, sf in parser.scale_factors.items():
        if 'met' in sf['name'].lower() or 'hourly' in sf['name'].lower():
            aces_config['meteorology'][sf['name'].lower()] = sf['name']

    # Diagnostics and Grid
    diagnostics = {}

    # Grid parsing
    grid_file = parser.settings.get('GridFile')
    if grid_file:
        full_grid_path = os.path.join(base_dir, grid_file) if not os.path.isabs(grid_file) else grid_file
        grid_parser = GridParser(full_grid_path)
        if grid_parser.grid_params:
            diagnostics['grid_type'] = 'gaussian' # Default assumption
            if 'NLON' in grid_parser.grid_params:
                diagnostics['nx'] = int(grid_parser.grid_params['NLON'])
            if 'NLAT' in grid_parser.grid_params:
                diagnostics['ny'] = int(grid_parser.grid_params['NLAT'])

    # Default values if not found
    if 'output_interval' not in diagnostics:
        diag_freq = parser.settings.get('DiagnFreq', '00000000 010000')
        try:
            parts = diag_freq.split()
            if len(parts) == 2:
                time_str = parts[1]
                h = int(time_str[0:2])
                m = int(time_str[2:4])
                s = int(time_str[4:6])
                diagnostics['output_interval'] = h * 3600 + m * 60 + s
            else:
                diagnostics['output_interval'] = 3600
        except:
            diagnostics['output_interval'] = 3600

    # DiagnFile parsing
    diagn_file = parser.settings.get('DiagnFile')
    if diagn_file:
        full_diagn_path = os.path.join(base_dir, diagn_file) if not os.path.isabs(diagn_file) else diagn_file
        diagn_parser = DiagnParser(full_diagn_path)
        if diagn_parser.variables:
            diagnostics['variables'] = diagn_parser.variables

    if diagnostics:
        aces_config['diagnostics'] = diagnostics

    with open(output_path, 'w') as f:
        yaml.dump(aces_config, f, sort_keys=False)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Convert HEMCO configuration to ACES configuration.')
    parser.add_argument('hemco_config', help='Path to the HEMCO configuration file (.rc)')
    parser.add_argument('-o', '--output', default='aces_config.yaml', help='Path to the output ACES configuration file (default: aces_config.yaml)')

    args = parser.parse_args()
    convert_hemco_to_aces(args.hemco_config, args.output)
    print(f"Successfully converted {args.hemco_config} to {args.output}")
