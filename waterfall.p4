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

#define WATERFALL_WIDTH 1024
#define WATERFALL_BIT_WIDTH 10 // 2^WATERFALL_BIT_WIDTH = WATERFALL_WIDTH

struct metadata_t {
  bit<WATERFALL_BIT_WIDTH> idx1;
  bool found;
  bit<WATERFALL_BIT_WIDTH> idx2;
}

// ---------------------------------------------------------------------------
// Ingress parser
// ---------------------------------------------------------------------------
parser SwitchIngressParser(packet_in pkt, out header_t hdr, out metadata_t ig_md,
                    out ingress_intrinsic_metadata_t ig_intr_md) {

  TofinoIngressParser() tofino_parser;

  state start {
    tofino_parser.apply(pkt, ig_intr_md);
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
    transition accept;
  }

  state parse_udp {
    pkt.extract(hdr.udp);
    transition accept;
  }
}
// ---------------------------------------------------------------------------
// Ingress Deparser
// ---------------------------------------------------------------------------
control SwitchIngressDeparser( packet_out pkt, inout header_t hdr, in metadata_t ig_md,
    in ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md) {
  apply {
    pkt.emit(hdr);
  }
}

control SwitchIngress(inout header_t hdr, inout metadata_t ig_md,
              in ingress_intrinsic_metadata_t ig_intr_md,
              in ingress_intrinsic_metadata_from_parser_t ig_intr_prsr_md,
              inout ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md,
              inout ingress_intrinsic_metadata_for_tm_t ig_intr_tm_md) {

  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_1;  // src_addr
  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_2;  // dst_addr
  Register<bit<16>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_3;  // src_port
  Register<bit<16>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_4;  // dst_port
  Register<bit<8>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_5;   // protocol
  //
  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_1;  // src_addr
  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_2;  // dst_addr
  Register<bit<16>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_3;  // src_port
  Register<bit<16>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_4;  // dst_port
  Register<bit<8>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_5;   // protocol
  
  Hash<bit<WATERFALL_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash1;
  Hash<bit<WATERFALL_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash2;

  bit<32> key_1 = 0;

  RegisterAction<bit<32>, _, bool>(table_1_1) table_1_1_lookup = {
    void apply(inout bit<32> val, out bool read_value) {
      if (hdr.ipv4.src_addr == val) {
        read_value = true;
      } else {
        read_value = false;
      }
    }
  };


  RegisterAction<bit<32>, _, bit<32>>(table_1_1) table_1_1_swap = {
    void apply(inout bit<32> val, out bit<32> read_value) {
      read_value = val;
      val = key_1;
    }
  };
  action get_hash1() {
    ig_md.idx1 = hash1.get({hdr.ipv4.src_addr, 
                            hdr.ipv4.dst_addr, 
                            hdr.udp.src_port, 
                            hdr.udp.dst_port,
                            hdr.ipv4.protocol});
  }

  action get_hash2() {
    ig_md.idx2 = hash2.get({hdr.ipv4.src_addr, 
                            hdr.ipv4.dst_addr, 
                            hdr.udp.src_port, 
                            hdr.udp.dst_port,
                            hdr.ipv4.protocol});
  }


  action hit(PortId_t port) {
    ig_intr_tm_md.ucast_egress_port = port;
    ig_intr_dprsr_md.drop_ctl = 0x0;
  }

  action route(PortId_t dst_port) {
    ig_intr_tm_md.ucast_egress_port = dst_port;
    ig_intr_dprsr_md.drop_ctl = 0x0;
  }

  action miss() {
    ig_intr_dprsr_md.drop_ctl = 0x1; // Drop packet
  }
  table forward {
    key = { 
      hdr.ipv4.dst_addr : exact;
  }
  actions = { 
    route;
  }

  size = 1024;
 }


  apply { 
    get_hash1();
    get_hash2();

    key_1 = hdr.ipv4.src_addr;
    ig_md.found = table_1_1_lookup.execute(ig_md.idx1); 
    if (!ig_md.found) {
      table_1_1_swap.execute(ig_md.idx1);
      
    }
    forward.apply(); 
    ig_intr_tm_md.bypass_egress = 1w1;
  }
}

Pipeline(SwitchIngressParser(), SwitchIngress(), SwitchIngressDeparser(),
         EmptyEgressParser(), EmptyEgress(), EmptyEgressDeparser()) pipe;

Switch(pipe) main;
