/*******************************************************************************
 *  INTEL CONFIDENTIAL
 *
 *  Copyright (c) 2021 Intel Corporation
 *  All Rights Reserved.
 *
 *  This software and the related documents are Intel copyrighted materials,
 *  and your use of them is governed by the express license under which they
 *  were provided to you ("License"). Unless the License provides otherwise,
 *  you may not use, modify, copy, publish, distribute, disclose or transmit
 *  this software or the related documents without Intel's prior written
 *  permission.
 *
 *  This software and the related documents are provided as is, with no express
 *  or implied warranties, other than those that are expressly stated in the
 *  License.
 ******************************************************************************/

#include <core.p4>
#if __TARGET_TOFINO__ == 3
#include <t3na.p4>
#elif __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif

#include "common/headers.p4"
#include "common/util.p4"

#define WATERFALL_BIT_WIDTH 16 
#define WATERFALL_REMAIN_BIT_WIDTH 16 // 32 - WATERFALL_BIT_WIDTH
#define WATERFALL_WIDTH 65536 // 2 ^ WATERFALL_BIT_WIDTH = WATERFALL_WIDTH

const bit<8> RESUB = 1;
const bit<3> DPRSR_RESUB = 1;

header resubmit_md_t {
  bit<16> idx;
  bit<16> remain;
}

struct port_metadata_t {
  bit<32> f1;
  bit<32> f2;
}

struct digest_t {
  bit<32> src_addr;
  bit<32> dst_addr;
  bit<16> src_port;
  bit<16> dst_port;
  bit<8> protocol;
}

struct metadata_t {
  port_metadata_t port_metadata;
  resubmit_md_t resubmit_md;
  bit<WATERFALL_BIT_WIDTH> idx1;
  bit<WATERFALL_BIT_WIDTH> idx2;
  bit<WATERFALL_REMAIN_BIT_WIDTH> remain1;
  bit<WATERFALL_REMAIN_BIT_WIDTH> remain2;
  bool found;
  bit<16> src_port;
  bit<16> dst_port;
}

// ---------------------------------------------------------------------------
// Ingress parser
// ---------------------------------------------------------------------------
parser SwitchIngressParser(packet_in pkt, out header_t hdr, out metadata_t ig_md,
                    out ingress_intrinsic_metadata_t ig_intr_md) {

  state start {
    pkt.extract(ig_intr_md);
    transition select(ig_intr_md.resubmit_flag) {
      0 : parse_init;
      1 : parse_resubmit;
    }
  }

  state parse_init {
    ig_md.port_metadata = port_metadata_unpack<port_metadata_t>(pkt);
    transition parse_ethernet;
  }

  state parse_resubmit {
    pkt.extract(ig_md.resubmit_md);
    transition parse_ethernet;
  }

  state parse_ethernet {
    pkt.extract(hdr.ethernet);
    transition select(hdr.ethernet.ether_type) {
    ETHERTYPE_IPV4:
      parse_ipv4;
    default:
      reject;
    }
  }

  state parse_ipv4 {
    pkt.extract(hdr.ipv4);
    transition select(hdr.ipv4.protocol) {
    IP_PROTOCOLS_UDP:
      parse_udp;
    IP_PROTOCOLS_TCP:
      parse_tcp;
    default:
      accept;
    }
  }

  state parse_tcp {
    pkt.extract(hdr.tcp);
    ig_md.src_port = hdr.tcp.src_port;
    ig_md.dst_port = hdr.tcp.dst_port;
    transition accept;
  }

  state parse_udp {
    pkt.extract(hdr.udp);
    ig_md.src_port = hdr.udp.src_port;
    ig_md.dst_port = hdr.udp.dst_port;
    transition accept;
  }
}
// ---------------------------------------------------------------------------
// Ingress Deparser
// ---------------------------------------------------------------------------
control SwitchIngressDeparser( packet_out pkt, inout header_t hdr, in metadata_t ig_md,
    in ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md) {

  Digest<digest_t>() digest;
  Resubmit() resubmit;

  apply {
    if (ig_intr_dprsr_md.digest_type == 1) {
      digest.pack({hdr.ipv4.src_addr, hdr.ipv4.dst_addr, ig_md.src_port, ig_md.dst_port, hdr.ipv4.protocol});
    }
    if (ig_intr_dprsr_md.resubmit_type == DPRSR_RESUB) {
      resubmit.emit(ig_md.resubmit_md);
    }
    pkt.emit(hdr);
  }
}

control SwitchIngress(inout header_t hdr, inout metadata_t ig_md,
              in ingress_intrinsic_metadata_t ig_intr_md,
              in ingress_intrinsic_metadata_from_parser_t ig_intr_prsr_md,
              inout ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md,
              inout ingress_intrinsic_metadata_for_tm_t ig_intr_tm_md) {

  Register<bit<WATERFALL_REMAIN_BIT_WIDTH>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1; 
  Register<bit<WATERFALL_REMAIN_BIT_WIDTH>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2;
  
  CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial
                         true,          // reversed
                         false,         // use msb?
                         false,         // extended?
                         32w0xFFFFFFFF, // initial shift register value
                         32w0xFFFFFFFF  // result xor
                         ) CRC32_1;
  Hash<bit<32>>(HashAlgorithm_t.CUSTOM, CRC32_1) hash1;

  CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial
                         true,          // reversed
                         false,         // use msb?
                         false,         // extended?
                         32w0xFFFFFFF0, // initial shift register value
                         32w0xFFFFFFFF  // result xor
                         ) CRC32_2;
  Hash<bit<32>>(HashAlgorithm_t.CUSTOM, CRC32_2) hash2;

  RegisterAction<bit<WATERFALL_REMAIN_BIT_WIDTH>, bit<WATERFALL_BIT_WIDTH>, bool>(table_1) table_1_lookup = {
    void apply(inout bit<WATERFALL_REMAIN_BIT_WIDTH> val, out bool read_value) {
      if (ig_md.remain1 == val) {
        read_value = true;
      } else {
        read_value = false;
      }
    }
  };

  RegisterAction<bit<WATERFALL_REMAIN_BIT_WIDTH>, bit<WATERFALL_BIT_WIDTH>, bool>(table_2) table_2_lookup = {
    void apply(inout bit<WATERFALL_REMAIN_BIT_WIDTH> val, out bool read_value) {
      if (ig_md.remain2 == val) {
        read_value = true;
      } else {
        read_value = false;
      }
    }
  };

  RegisterAction<bit<WATERFALL_REMAIN_BIT_WIDTH>, bit<WATERFALL_BIT_WIDTH>, bit<WATERFALL_REMAIN_BIT_WIDTH>>(table_1) table_1_swap = {
    void apply(inout bit<WATERFALL_REMAIN_BIT_WIDTH> val, out bit<WATERFALL_REMAIN_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.resubmit_md.remain;
    }
  };

  RegisterAction<bit<WATERFALL_REMAIN_BIT_WIDTH>, _, bit<WATERFALL_REMAIN_BIT_WIDTH>>(table_2) table_2_swap = {
    void apply(inout bit<WATERFALL_REMAIN_BIT_WIDTH> val, out bit<WATERFALL_REMAIN_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain2;
    }
  };

  action get_hash1(bit<32> src_addr, bit<32> dst_addr, bit<32> ports, bit<8> protocol) {
    bit<32> hash_val = hash1.get({src_addr, dst_addr, ports, protocol});
    ig_md.idx1 = hash_val[WATERFALL_BIT_WIDTH - 1:0];
    ig_md.remain1 = hash_val[31:WATERFALL_BIT_WIDTH];
  }

  action get_hash2(bit<32> src_addr, bit<32> dst_addr, bit<32> ports, bit<8> protocol) {
    bit<32> hash_val = hash2.get({src_addr, dst_addr, ports, protocol});
    ig_md.idx2 = hash_val[WATERFALL_BIT_WIDTH - 1:0];
    ig_md.remain2 = hash_val[31:WATERFALL_BIT_WIDTH];
  }

  action resubmit_hdr() {
    ig_intr_dprsr_md.resubmit_type = DPRSR_RESUB;
    ig_intr_dprsr_md.digest_type = 1;
    ig_md.resubmit_md.idx = ig_md.idx1;
    ig_md.resubmit_md.remain = ig_md.remain1;
  }

  action no_resub() {}
  action drop() { ig_intr_dprsr_md.drop_ctl = 1; }

  table resub {
    key = {
      ig_md.found : exact;
    }
    actions = {
      resubmit_hdr;
      no_resub;
      drop;
    }
    default_action = no_resub;
    size = 256;
  }


  bit<32> key_1 = 0;
  bit<32> key_2 = 0;
  bit<32> ports = 0;
  bit<8> proto = 0;

  apply { 
    if (ig_intr_md.resubmit_flag == 0) {
      key_1 = hdr.ipv4.src_addr;
      key_2 = hdr.ipv4.dst_addr;
      ports = ig_md.src_port ++ ig_md.dst_port;
      proto = hdr.ipv4.protocol;
      get_hash1(key_1, key_2, ports, proto);
      key_1 = ig_md.idx1 ++ ig_md.remain1;
      key_2 = 0;
      ports = 0;
      proto = 0;
      get_hash2(key_1, key_2, ports, proto);
      bool found_t_1 = table_1_lookup.execute(ig_md.idx1); 
      bool found_t_2 = table_2_lookup.execute(ig_md.idx2); 
      if (found_t_1 || found_t_2) {
        ig_md.found = true;
      } else {
        ig_md.found = false;
      }
      
      resub.apply();
    } else {
      bit<16> remain1 = table_1_swap.execute(ig_md.resubmit_md.idx);
      if ( remain1 != 0x0) {
        key_1 = ig_md.resubmit_md.idx ++ remain1;
        key_2 = 0;
        ports = 0;
        proto = 0;
        get_hash2(key_1, key_2, ports, proto);
        bit<16> remain2 = table_2_swap.execute(ig_md.idx2);
      }
    }

    ig_intr_tm_md.ucast_egress_port = ig_intr_md.ingress_port;
    ig_intr_tm_md.bypass_egress = 1w1;
  }
}

Pipeline(SwitchIngressParser(), SwitchIngress(), SwitchIngressDeparser(),
         EmptyEgressParser(), EmptyEgress(), EmptyEgressDeparser()) pipe;

Switch(pipe) main;
