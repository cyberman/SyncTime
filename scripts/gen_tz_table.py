#!/usr/bin/env python3
"""
gen_tz_table.py - Generate tz_table.c from IANA tzdb source files

This script parses IANA tzdb Zone and Rule definitions to extract current
timezone offsets and DST transition rules, outputting tz_table.c for
compilation into SyncTime.

Usage: python3 scripts/gen_tz_table.py tzdata-dir > src/tz_table.c

Zone format example:
    Zone America/Los_Angeles -7:52:58 -  LMT    1883 Nov 18 20:00u
                -8:00   US  P%sT   1946
                -8:00   CA  P%sT   1967
                -8:00   US  P%sT
    The last line (no UNTIL) is the current rule.

Rule format example:
    Rule   US  2007  max  -  Mar  Sun>=8  2:00  1:00  D
    Rule   US  2007  max  -  Nov  Sun>=1  2:00  0     S
    Where: name, from_year, to_year, type, month, ON (day spec), AT (time), SAVE, LETTER

ON field formats:
    lastSun   - last Sunday of month
    Sun>=8    - first Sunday on or after 8th (second Sunday)
    Sun>=1    - first Sunday of month
    15        - specific day number (we convert to week/dow approximation)
"""

import sys
import os
import re
from dataclasses import dataclass, field
from datetime import date
from typing import Dict, List, Optional, Tuple

# Source files to parse (in order)
TZDB_FILES = [
    'africa',
    'antarctica',
    'asia',
    'australasia',
    'europe',
    'northamerica',
    'southamerica',
    'etcetera',
]

# Month name to number mapping
MONTHS = {
    'Jan': 1, 'Feb': 2, 'Mar': 3, 'Apr': 4, 'May': 5, 'Jun': 6,
    'Jul': 7, 'Aug': 8, 'Sep': 9, 'Oct': 10, 'Nov': 11, 'Dec': 12
}

# Day of week mapping
DAYS = {
    'Sun': 0, 'Mon': 1, 'Tue': 2, 'Wed': 3,
    'Thu': 4, 'Fri': 5, 'Sat': 6
}


@dataclass
class DSTRule:
    """A single DST transition rule (start or end)."""
    month: int = 0        # 1-12
    week: int = 0         # 1-5, 5=last
    dow: int = 0          # 0=Sun, 6=Sat
    hour: int = 0         # Hour of transition
    offset_mins: int = 0  # Offset to add during this period


@dataclass
class RuleSet:
    """A named set of DST rules (e.g., 'US', 'EU')."""
    name: str
    rules: List[Tuple[int, int, DSTRule]] = field(default_factory=list)
    # List of (from_year, to_year, rule) tuples


@dataclass
class TZEntry:
    """A timezone entry for output."""
    name: str
    region: str
    city: str
    std_offset_mins: int
    dst_offset_mins: int = 0
    dst_start_month: int = 0
    dst_start_week: int = 0
    dst_start_dow: int = 0
    dst_start_hour: int = 0
    dst_end_month: int = 0
    dst_end_week: int = 0
    dst_end_dow: int = 0
    dst_end_hour: int = 0


def parse_offset(offset_str: str) -> int:
    """Parse a time offset string into minutes.

    Examples:
        '-8:00' -> -480
        '5:30' -> 330
        '-7:52:58' -> -472 (rounds to nearest minute)
        '0' -> 0
        '-0:16:08' -> -16
    """
    if offset_str == '-' or offset_str == '0':
        return 0

    negative = offset_str.startswith('-')
    if negative:
        offset_str = offset_str[1:]

    parts = offset_str.split(':')
    hours = int(parts[0])
    minutes = int(parts[1]) if len(parts) > 1 else 0
    seconds = int(parts[2]) if len(parts) > 2 else 0

    # Round to nearest minute
    total_mins = hours * 60 + minutes + (1 if seconds >= 30 else 0)

    return -total_mins if negative else total_mins


def parse_time(time_str: str) -> int:
    """Parse a time string into hours (ignoring suffix like u, s, w).

    Examples:
        '2:00' -> 2
        '1:00u' -> 1
        '23:00s' -> 23
    """
    # Remove suffix (u=UTC, s=standard, w=wall)
    time_str = time_str.rstrip('usw')

    parts = time_str.split(':')
    return int(parts[0])


def parse_on_field(on_str: str) -> Tuple[int, int]:
    """Parse the ON field to get (week, dow).

    Returns:
        (week, dow) where week is 1-5 (5=last) and dow is 0-6 (0=Sun)

    Examples:
        'lastSun' -> (5, 0)
        'Sun>=8' -> (2, 0)  # Second Sunday (first on or after 8th)
        'Sun>=1' -> (1, 0)  # First Sunday
        'Sun>=15' -> (3, 0) # Third Sunday (first on or after 15th)
        'Sun>=22' -> (4, 0) # Fourth Sunday
        '15' -> (0, 0)      # Specific day (will be approximated)
    """
    # Handle 'lastXXX' format
    if on_str.startswith('last'):
        dow_str = on_str[4:]
        dow = DAYS.get(dow_str, 0)
        return (5, dow)  # 5 = last

    # Handle 'DOW>=N' format
    match = re.match(r'(\w+)>=(\d+)', on_str)
    if match:
        dow_str = match.group(1)
        day_num = int(match.group(2))
        dow = DAYS.get(dow_str, 0)

        # Convert day-of-month to week number
        # 1-7 = 1st week, 8-14 = 2nd week, 15-21 = 3rd week, 22-28 = 4th week
        week = (day_num - 1) // 7 + 1
        return (week, dow)

    # Handle specific day number (rare in modern rules)
    # This is an approximation - we'll use week 3 (middle of month) with Sunday
    try:
        day_num = int(on_str)
        week = (day_num - 1) // 7 + 1
        return (week, 0)
    except ValueError:
        pass

    # Unknown format - default to first Sunday
    return (1, 0)


def parse_save_field(save_str: str) -> int:
    """Parse the SAVE field to get DST offset in minutes.

    Examples:
        '1:00' -> 60
        '0' -> 0
        '0:30' -> 30
    """
    if save_str == '0' or save_str == '-':
        return 0
    return parse_offset(save_str)


def parse_rules(tzdb_dir: str) -> Dict[str, RuleSet]:
    """Parse all Rule definitions from tzdb files.

    Returns a dict mapping rule name to RuleSet.
    """
    rules: Dict[str, RuleSet] = {}

    for filename in TZDB_FILES:
        filepath = os.path.join(tzdb_dir, filename)
        if not os.path.exists(filepath):
            continue

        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                # Remove comments
                if '#' in line:
                    line = line[:line.index('#')]
                line = line.strip()

                if not line.startswith('Rule'):
                    continue

                parts = line.split()
                if len(parts) < 10:
                    continue

                # Rule NAME FROM TO - IN ON AT SAVE LETTER
                name = parts[1]
                from_year = parts[2]
                to_year = parts[3]
                # parts[4] is '-' (type, obsolete)
                month_str = parts[5]
                on_str = parts[6]
                at_str = parts[7]
                save_str = parts[8]

                # Parse from/to years
                if from_year == 'min':
                    from_yr = 1900
                elif from_year == 'max':
                    from_yr = 9999
                else:
                    from_yr = int(from_year)

                if to_year == 'only':
                    to_yr = from_yr
                elif to_year == 'max':
                    to_yr = 9999
                else:
                    to_yr = int(to_year)

                # Parse rule details
                month = MONTHS.get(month_str, 1)
                week, dow = parse_on_field(on_str)
                hour = parse_time(at_str)
                offset_mins = parse_save_field(save_str)

                rule = DSTRule(
                    month=month,
                    week=week,
                    dow=dow,
                    hour=hour,
                    offset_mins=offset_mins
                )

                if name not in rules:
                    rules[name] = RuleSet(name=name)
                rules[name].rules.append((from_yr, to_yr, rule))

    return rules


def get_current_rules(ruleset: RuleSet) -> Tuple[Optional[DSTRule], Optional[DSTRule]]:
    """Get the current DST start and end rules from a ruleset.

    Returns (start_rule, end_rule) for the current year and beyond.
    A rule is 'current' if to_year == 9999 (max).
    Start rule has offset > 0, end rule has offset == 0.
    """
    current_year = date.today().year

    start_rule = None
    end_rule = None

    for from_yr, to_yr, rule in ruleset.rules:
        if to_yr >= current_year and from_yr <= current_year:
            if rule.offset_mins > 0:
                start_rule = rule
            else:
                end_rule = rule

    return start_rule, end_rule


def parse_zones(tzdb_dir: str, rules: Dict[str, RuleSet]) -> List[TZEntry]:
    """Parse all Zone definitions and create TZEntry objects.

    Only the last (current) line of each zone is used.
    """
    zones: List[TZEntry] = []
    current_zone: Optional[str] = None
    current_offset: int = 0
    current_rule_name: Optional[str] = None

    for filename in TZDB_FILES:
        filepath = os.path.join(tzdb_dir, filename)
        if not os.path.exists(filepath):
            continue

        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                # Remove comments
                if '#' in line:
                    line = line[:line.index('#')]
                line = line.rstrip()

                if not line.strip():
                    continue

                # Check for Zone definition start
                if line.startswith('Zone'):
                    # Save previous zone if it exists
                    if current_zone:
                        zones.append(create_tz_entry(
                            current_zone, current_offset, current_rule_name, rules
                        ))

                    parts = line.split()
                    if len(parts) < 3:
                        current_zone = None
                        continue

                    current_zone = parts[1]
                    current_offset = parse_offset(parts[2])
                    current_rule_name = parts[3] if len(parts) > 3 and parts[3] != '-' else None

                    # If there's an UNTIL column (parts >= 5 with year), this isn't the current rule
                    # Keep parsing continuation lines

                # Check for Zone continuation (starts with whitespace)
                elif current_zone and (line.startswith('\t') or line.startswith(' ')):
                    parts = line.split()
                    if len(parts) >= 2:
                        # This is a continuation line
                        current_offset = parse_offset(parts[0])
                        current_rule_name = parts[1] if len(parts) > 1 and parts[1] != '-' else None
                        # If no UNTIL (len < 4 or no year after FORMAT), this is current

                # Link definition - we skip these as they're aliases
                elif line.startswith('Link'):
                    pass

    # Don't forget the last zone
    if current_zone:
        zones.append(create_tz_entry(
            current_zone, current_offset, current_rule_name, rules
        ))

    return zones


def create_tz_entry(zone_name: str, std_offset: int, rule_name: Optional[str],
                    rules: Dict[str, RuleSet]) -> TZEntry:
    """Create a TZEntry from zone information and rules."""
    # Parse region and city from zone name
    if '/' in zone_name:
        parts = zone_name.split('/', 1)
        region = parts[0]
        city = parts[1]
    else:
        region = zone_name
        city = zone_name

    entry = TZEntry(
        name=zone_name,
        region=region,
        city=city,
        std_offset_mins=std_offset
    )

    # Look up DST rules if a rule name is specified
    if rule_name and rule_name in rules:
        ruleset = rules[rule_name]
        start_rule, end_rule = get_current_rules(ruleset)

        if start_rule and end_rule:
            entry.dst_offset_mins = start_rule.offset_mins
            entry.dst_start_month = start_rule.month
            entry.dst_start_week = start_rule.week
            entry.dst_start_dow = start_rule.dow
            entry.dst_start_hour = start_rule.hour
            entry.dst_end_month = end_rule.month
            entry.dst_end_week = end_rule.week
            entry.dst_end_dow = end_rule.dow
            entry.dst_end_hour = end_rule.hour

    return entry


def generate_c_output(zones: List[TZEntry]) -> str:
    """Generate the C source file content."""
    # Sort zones by name
    zones.sort(key=lambda z: z.name)

    lines = []
    lines.append('/* tz_table.c - Generated timezone table from IANA tzdb */')
    lines.append('/* DO NOT EDIT - Generated by scripts/gen_tz_table.py */')
    lines.append('')
    lines.append('#include "synctime.h"')
    lines.append('')
    lines.append('const TZEntry tz_table[] = {')

    for zone in zones:
        # Escape any special characters in strings
        name = zone.name.replace('\\', '\\\\').replace('"', '\\"')
        region = zone.region.replace('\\', '\\\\').replace('"', '\\"')
        city = zone.city.replace('\\', '\\\\').replace('"', '\\"')

        line = f'    {{"{name}", "{region}", "{city}", '
        line += f'{zone.std_offset_mins}, {zone.dst_offset_mins}, '
        line += f'{zone.dst_start_month}, {zone.dst_start_week}, '
        line += f'{zone.dst_start_dow}, {zone.dst_start_hour}, '
        line += f'{zone.dst_end_month}, {zone.dst_end_week}, '
        line += f'{zone.dst_end_dow}, {zone.dst_end_hour}}},'
        lines.append(line)

    lines.append('};')
    lines.append('')
    lines.append(f'const ULONG tz_table_count = {len(zones)};')
    lines.append('')

    return '\n'.join(lines)


def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <tzdb-directory>", file=sys.stderr)
        print("", file=sys.stderr)
        print("Parses IANA tzdb source files and generates tz_table.c", file=sys.stderr)
        print("", file=sys.stderr)
        print("Example: python3 scripts/gen_tz_table.py tzdb-2025c > src/tz_table.c", file=sys.stderr)
        sys.exit(1)

    tzdb_dir = sys.argv[1]

    if not os.path.isdir(tzdb_dir):
        print(f"Error: '{tzdb_dir}' is not a directory", file=sys.stderr)
        sys.exit(1)

    # Parse rules first
    print(f"Parsing rules from {tzdb_dir}...", file=sys.stderr)
    rules = parse_rules(tzdb_dir)
    print(f"Found {len(rules)} rule sets", file=sys.stderr)

    # Parse zones
    print(f"Parsing zones from {tzdb_dir}...", file=sys.stderr)
    zones = parse_zones(tzdb_dir, rules)
    print(f"Found {len(zones)} zones", file=sys.stderr)

    # Filter out zones without '/' and Etc/ zones
    zones = [z for z in zones if '/' in z.name and not z.name.startswith('Etc/')]
    print(f"After filtering: {len(zones)} zones", file=sys.stderr)

    # Generate output
    output = generate_c_output(zones)
    print(output)

    print(f"Generated tz_table.c with {len(zones)} entries", file=sys.stderr)


if __name__ == '__main__':
    main()
