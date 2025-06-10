/**
 * eBPF Map key&value structure definitions for both ingress and egress traffic.
 * 
 * Checked-in date: June-9, 2025
 * Author: xmei@jlab.org, ChatGPT
 * Test: "nvidarm" Host
 */


#ifndef TC_COMMON_H
#define TC_COMMON_H

#include <linux/types.h>

struct traffic_key_t {
    __u32 ip;
    // Use the standard protocol numbers denoted as IPPROTO_TCP/IPPROTO_XXX in <linux/in.h>.
    // Number reference: https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
    __u8 proto;
    // eBPF map keys must be aligned to 4/8/... bytes to pass the kernel verifier.
    __u8 pad[3];  // padding for alignment
};

struct traffic_val_t {
    __u64 packets;
    __u64 bytes;
};

#endif
