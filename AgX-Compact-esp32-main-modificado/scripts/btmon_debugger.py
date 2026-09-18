#!/usr/bin/env python3
import sys

def process_line(line):
    # Remove any leading/trailing whitespace/newlines.
    line = line.strip()
    # Look for the hex pattern "3e4f54"
    idx = line.find("3e4f54") # ">OT" in hex
    if idx == -1:
        # print(line)
        return  # Pattern not found, skip this line.
    
    # Extract from the pattern to the end of the line.
    hex_substr = line[idx:]
    
    try:
        # Convert the hex substring to a bytes object, then decode to ASCII.
        ascii_output = bytes.fromhex(hex_substr).decode("ascii", errors="backslashreplace")
        print(ascii_output, flush=True)
    except Exception as e:
        print(f"Error converting hex to ASCII: {e}", file=sys.stderr)

def main():
    # Continuously read lines from standard input.
    for line in sys.stdin:
        process_line(line)

if __name__ == "__main__":
    main()
