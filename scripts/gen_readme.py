#!/usr/bin/env python3
"""
Generate SyncTime.readme (Aminet format) from README.md and template header.
"""

import re
import sys
import textwrap

HEADER_TEMPLATE = """\
Short:        NTP time sync commodity with timezone support
Author:       Nathan Ollerenshaw <chrome@stupendous.net>
Uploader:     chrome@stupendous.net
Type:         util/time
Version:      {version}
Architecture: m68k-amigaos >= 3.2.0
Requires:     AmigaOS 3.2+, bsdsocket.library, TCP/IP stack
Distribution: Aminet
"""

HISTORY = """\
History:

  1.0.1 - Build system improvements
        - Switched to CachyOS for CI builds

  1.0.0 - Initial release
        - SNTP synchronization
        - Reaction GUI with timezone picker
        - IANA timezone database with DST
        - TZ/TZONE environment variable support
"""

def strip_markdown(text):
    """Convert markdown to plain text."""
    # Remove code blocks
    text = re.sub(r'```[^`]*```', '', text, flags=re.DOTALL)
    # Remove inline code
    text = re.sub(r'`([^`]+)`', r'\1', text)
    # Remove bold/italic
    text = re.sub(r'\*\*([^*]+)\*\*', r'\1', text)
    text = re.sub(r'\*([^*]+)\*', r'\1', text)
    # Remove links, keep text
    text = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', text)
    # Remove markdown tables (we'll handle them specially)
    return text

def parse_readme(readme_path):
    """Parse README.md into sections."""
    with open(readme_path, 'r') as f:
        content = f.read()

    sections = {}
    current_section = 'intro'
    current_content = []

    for line in content.split('\n'):
        if line.startswith('## '):
            if current_content:
                sections[current_section] = '\n'.join(current_content).strip()
            current_section = line[3:].strip().lower()
            current_content = []
        elif line.startswith('# '):
            # Skip main title
            pass
        else:
            current_content.append(line)

    if current_content:
        sections[current_section] = '\n'.join(current_content).strip()

    return sections

def format_list(text, indent=2):
    """Format markdown list to indented plain text."""
    lines = []
    for line in text.split('\n'):
        line = line.strip()
        if line.startswith('- '):
            lines.append(' ' * indent + '* ' + line[2:])
        elif line.startswith('| ') and not line.startswith('|--'):
            # Table row - skip header separator
            pass
        elif line:
            lines.append(' ' * indent + line)
    return '\n'.join(lines)

def format_section(title, content):
    """Format a section with title and indented content."""
    # Strip markdown
    content = strip_markdown(content)

    # Format lists
    lines = []
    for line in content.split('\n'):
        line = line.rstrip()
        if line.startswith('- '):
            lines.append('  * ' + line[2:])
        elif line.startswith('|'):
            # Skip markdown tables
            continue
        elif line:
            lines.append('  ' + line)

    if not lines:
        return ''

    return f"{title}:\n\n" + '\n'.join(lines)

def generate_readme(readme_path, version):
    """Generate Aminet-style readme."""
    sections = parse_readme(readme_path)

    output = []
    output.append(HEADER_TEMPLATE.format(version=version))

    # Description from intro
    if 'intro' in sections:
        desc = strip_markdown(sections['intro']).strip()
        output.append(desc)

    # Features
    if 'features' in sections:
        output.append('\n' + format_section('Features', sections['features']))

    # Installation
    if 'installation' in sections:
        output.append('\n' + format_section('Installation', sections['installation']))

    # Usage
    if 'usage' in sections:
        output.append('\n' + format_section('Usage', sections['usage']))

    # Tooltypes
    if 'tooltypes' in sections:
        output.append('\nTooltypes/CLI Arguments:\n')
        output.append('  CX_PRIORITY=<n>   Commodity priority (default: 0)')
        output.append('  CX_POPUP=YES|NO   Open window on startup (default: NO)')
        output.append('  CX_POPKEY=<key>   Hotkey to toggle window (default: ctrl alt s)')
        output.append('  DONOTWAIT         Workbench won\'t wait for exit (for WBStartup)')

    # Requirements
    if 'requirements' in sections:
        output.append('\n' + format_section('Requirements', sections['requirements']))

    # Source
    output.append('\nSource Code:\n')
    output.append('  https://github.com/matjam/synctime')

    # History
    output.append('\n' + HISTORY)

    # License
    output.append('License:\n')
    output.append('  This software is released under the MIT License.')

    # Contact
    output.append('\nContact:\n')
    output.append('  Nathan Ollerenshaw <chrome@stupendous.net>')
    output.append('')

    return '\n'.join(output)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <README.md> <version>", file=sys.stderr)
        sys.exit(1)

    readme_path = sys.argv[1]
    version = sys.argv[2]

    print(generate_readme(readme_path, version))
