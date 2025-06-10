"""
A helper program to convert a networking u32 number into an
 IPv4 address printed as xx.xx.xx.xx

Example:
$ python3 ntoh_IP_convert.py 112277889
IP address: 129.57.177.6 
"""

import sys
import socket
import struct

if len(sys.argv) != 2:
    print(f"Usage: {sys.argv[0]} <u32 number in network byte order>")
    sys.exit(1)

# Parse input number (supports decimal or hex like 0xC0A80101)
ip_net = int(sys.argv[1], 0)

# Convert from network to host byte order
ip_host = socket.ntohl(ip_net)

# Convert to dotted-decimal string
ip_str = socket.inet_ntoa(struct.pack("!I", ip_host))

print("IP address:", ip_str)
