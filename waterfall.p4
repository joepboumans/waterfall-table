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

#define COUNTER_WIDTH 1024
#define COUNTER_BIT_WIDTH 10 // 2^COUNTER_BIT_WIDTH = COUNTER_WIDTH

struct metadata_t {
  bit<32> count1;
  bit<32> count2;
  bit<32> count3;
  bit<32> count4;
  bit<COUNTER_BIT_WIDTH> idx1;
  bit<COUNTER_BIT_WIDTH> idx2;
  bit<COUNTER_BIT_WIDTH> idx3;
  bit<COUNTER_BIT_WIDTH> idx4;
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

  Hash<bit<COUNTER_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash1;
  Hash<bit<COUNTER_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash2;
  Hash<bit<COUNTER_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash3;
  Hash<bit<COUNTER_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash4;
  Register<bit<32>, bit<COUNTER_BIT_WIDTH>>(COUNTER_WIDTH, 0) counter1;
  Register<bit<32>, bit<COUNTER_BIT_WIDTH>>(COUNTER_WIDTH, 0) counter2;
  Register<bit<32>, bit<COUNTER_BIT_WIDTH>>(COUNTER_WIDTH, 0) counter3;
  Register<bit<32>, bit<COUNTER_BIT_WIDTH>>(COUNTER_WIDTH, 0) counter4;

  RegisterAction<bit<32>, bit<COUNTER_BIT_WIDTH>, bit<32>>(counter1) inc_counter_write1 = {
    void apply(inout bit<32> val, out bit<32> out_val) {
      val = val + 1;
      out_val = val;
    }
  };

  RegisterAction<bit<32>, bit<COUNTER_BIT_WIDTH>, bit<32>>(counter2) inc_counter_write2 = {
    void apply(inout bit<32> val, out bit<32> out_val) {
      val = val + 1;
      out_val = val;
    }
  };

  RegisterAction<bit<32>, bit<COUNTER_BIT_WIDTH>, bit<32>>(counter3) inc_counter_write3 = {
    void apply(inout bit<32> val, out bit<32> out_val) {
      val = val + 1;
      out_val = val;
    }
  };

  RegisterAction<bit<32>, bit<COUNTER_BIT_WIDTH>, bit<32>>(counter4) inc_counter_write4 = {
    void apply(inout bit<32> val, out bit<32> out_val) {
      val = val + 1;
      out_val = val;
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

  action get_hash3() {
    ig_md.idx3 = hash3.get({hdr.ipv4.src_addr, 
                            hdr.ipv4.dst_addr, 
                            hdr.udp.src_port, 
                            hdr.udp.dst_port,
                            hdr.ipv4.protocol});
  }
  
  action get_hash4() {
    ig_md.idx4 = hash4.get({hdr.ipv4.src_addr, 
                            hdr.ipv4.dst_addr, 
                            hdr.udp.src_port, 
                            hdr.udp.dst_port,
                            hdr.ipv4.protocol});
  }

  action increment_counter1() {
    ig_md.count1 = inc_counter_write1.execute(ig_md.idx1);
  }

  action increment_counter2() {
    ig_md.count2 = inc_counter_write2.execute(ig_md.idx2);
  }

  action increment_counter3() {
    ig_md.count3 = inc_counter_write3.execute(ig_md.idx3);
  }

  action increment_counter4() {
    ig_md.count4 = inc_counter_write4.execute(ig_md.idx4);
  }

  action hit(PortId_t port) {
    ig_intr_tm_md.ucast_egress_port = port;
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
    get_hash3();
    get_hash4();
    increment_counter1();
    increment_counter2();
    increment_counter3();
    increment_counter4();

    forward.apply(); 
    ig_intr_tm_md.bypass_egress = 1w1;
  }
}

Pipeline(SwitchIngressParser(), SwitchIngress(), SwitchIngressDeparser(),
         EmptyEgressParser(), EmptyEgress(), EmptyEgressDeparser()) pipe;

Switch(pipe) main;
