#!/usr/bin/env python3

import hashlib
import sys

def compute_firmware_hash(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    
    # If the file is larger than 32 bytes, assume the last 32 bytes are the appended SHA-256 digest.
    if len(data) > 32:
        data = data[:-32]
    
    return hashlib.sha256(data).hexdigest()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: {} firmware.bin".format(sys.argv[0]))
        sys.exit(1)
    
    firmware_file = sys.argv[1]
    computed_hash = compute_firmware_hash(firmware_file)
    print("Computed SHA-256 hash:", computed_hash)
