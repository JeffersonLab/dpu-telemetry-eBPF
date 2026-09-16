/**
 * Checked-in date: June 9, 2025
 * Last updated: Sep 16, 2026
 *
 * eBPF Map (kernel) key&value structure definitions for both ingress and egress traffic.
 */


#ifndef TC_COMMON_H
#define TC_COMMON_H

#include <linux/types.h>

struct traffic_key_t {
    __u32 source_ip;
    __u32 destination_ip;
    // Use the standard protocol numbers denoted as IPPROTO_TCP/IPPROTO_XXX in <linux/in.h>.
    // Number reference: https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
    __u8 proto;
    // eBPF map keys must be aligned to 4/8/... bytes to pass the kernel verifier.
    __u8 pad[3];  // padding for alignment
};

struct traffic_val_t {
    // One block = one buffer (sk_buff) seen by the hook, not one wire packet.
    //  - GRO (Generic Receive Offload): on receive, the kernel merges consecutive
    //    wire packets of the same flow into one large sk_buff before the TC
    //    ingress hook runs, so one ingress block can hold many wire packets.
    //  - TSO (TCP Segmentation Offload) / GSO (Generic Segmentation Offload): on
    //    send, the stack hands the NIC one large sk_buff and the NIC (or GSO, just
    //    before the driver) splits it into wire packets after the TC egress hook,
    //    so one egress block can also hold many wire packets.
    // XDP runs in the driver before GRO, so there a block is a single frame.
    __u64 blocks;
    __u64 bytes;
};

#endif
