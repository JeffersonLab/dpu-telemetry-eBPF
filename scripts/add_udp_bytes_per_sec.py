import json
import sys

total_udp_bytes = 0

if len(sys.argv) < 2:
    print("Usage: python sum_udp_bytes_per_second.py <input_file>")
    sys.exit(1)

input_file = sys.argv[1]
total_udp_bytes = 0

with open(input_file, "r") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        try:
            data = json.loads(line)
            for ts_entry in data.values():  # e.g., "357971"
                for ip_entry in ts_entry.values():  # e.g., "2125543809"
                    total_udp_bytes = 0
                    udp_bytes = ip_entry.get("udp_bytes", [])
                    total_udp_bytes = sum(udp_bytes)
                    print(total_udp_bytes)
        except Exception as e:
            print(f"Failed to parse line: {e}")
