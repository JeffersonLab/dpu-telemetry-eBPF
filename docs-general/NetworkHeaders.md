
### Typical Network Packet Layout (as seen by eBPF)
```
+----------------------+  <- skb->data (start of packet)
| Ethernet Header      |  14 bytes (L2)
+----------------------+
| IP Header (IPv4/6)   |  20 bytes (IPv4 typical, variable for IPv6)
+----------------------+
| Transport Header     |  e.g., TCP or UDP (TCP = 20 bytes minimum)
+----------------------+
| Payload / Application Data |
+----------------------+
           |
      skb->data_end (end of packet)
```

#### IPv4 IP Header
20 Bytes minimum.
```c
// define in <linux/ip.h>

struct iphdr {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    __u8    version:4,
            ihl:4;
#else
    __u8    ihl:4,
            version:4;
#endif
    __u8    tos;
    __be16  tot_len;      // total length (header + data); __be for big-endian values
    __be16  id;
    __be16  frag_off;
    __u8    ttl;
    __u8    protocol;     // TCP=6, UDP=17, ICMP=1, etc.
    __be16  check;
    __be32  saddr;        // source IP address
    __be32  daddr;        // destination IP address
};
```

#### UDP Header
8 bytes, always fixed size.
```c
#include <linux/udp.h>

/*
+-----------------------+------------------------+
| Source Port (16 bits) | Destination Port (16b) |
+-----------------------+------------------------+
|     Length (16 bits)  |  Checksum (16 bits)    |
+-----------------------+------------------------+
*/
struct udphdr {
    __be16 source;   // Source port
    __be16 dest;     // Destination port
    __be16 len;      // Length (UDP header + payload)
    __be16 check;    // Checksum
};
```

#### TCP Header
20 bytes minimum, but can be longer.

```c
#include <linux/tcp.h>

/*
+-----------------------+------------------------+
| Source Port (16 bits) | Destination Port (16b) |
+-----------------------+------------------------+
|       Sequence Number (32 bits)               |
+-----------------------------------------------+
|     Acknowledgment Number (32 bits)           |
+-----------------------+------------------------+
| Data Offset (4 bits) | Flags, Window Size     |
+-----------------------+------------------------+
|     Checksum (16 bits)| Urgent Pointer (16b)  |
+-----------------------+------------------------+
|  Options (if any, variable)                   |
+-----------------------------------------------+

*/

struct tcphdr {
    __be16 source;     // Source port
    __be16 dest;       // Destination port
    __be32 seq;        // Sequence number
    __be32 ack_seq;    // Acknowledgment number

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    __u16 res1:4,      // Reserved
          doff:4,      // Data offset (header size in 32-bit words)
          fin:1,
          syn:1,
          rst:1,
          psh:1,
          ack:1,
          urg:1,
          ece:1,
          cwr:1;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    __u16 doff:4,
          res1:4,
          cwr:1,
          ece:1,
          urg:1,
          ack:1,
          psh:1,
          rst:1,
          syn:1,
          fin:1;
#else
# error  "Adjust your <bits/endian.h> defines"
#endif

    __be16 window;     // Window size
    __be16 check;      // Checksum
    __be16 urg_ptr;    // Urgent pointer
};
```
