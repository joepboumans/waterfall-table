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

#define WATERFALL_WIDTH 16
#define WATERFALL_BIT_WIDTH 4 // 2^WATERFALL_BIT_WIDTH = WATERFALL_WIDTH

const bit<8> RESUB = 1;
const bit<3> DPRSR_RESUB = 1;

header resubmit_md_t {
  bit<32> src_addr;
  bit<32> dst_addr;
}

struct port_metadata_t {
  bit<32> f1;
  bit<32> f2;
}

struct metadata_t {
  port_metadata_t port_metadata;
  resubmit_md_t resubmit_md;
  bit<32> src_addr;
  bit<32> dst_addr;
  bit<32> ports;
  bit<8> protocol;
  bit<WATERFALL_BIT_WIDTH> idx1;
  bit<WATERFALL_BIT_WIDTH> idx2;
  bool found;
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

  Resubmit() resubmit;

  apply {
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

  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_1;  // src_addr
  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_2;  // dst_addr
  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_3;  // src_port
  Register<bit<8>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_4;   // protocol
  //
  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_1;  // src_addr
  Register<bit<32>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_2;  // dst_addr
  Register<bit<16>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_3;  // src_port
  Register<bit<16>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_4;  // dst_port
  Register<bit<8>, bit<WATERFALL_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_5;   // protocol
  
  Hash<bit<WATERFALL_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash1;
  Hash<bit<WATERFALL_BIT_WIDTH>>(HashAlgorithm_t.CRC16) hash2;

  Register<bit<32>, bit<3>>(3, 0) store; 
  
  RegisterAction<bit<32>, _, bit<32>>(store) store_src = {
    void apply(inout bit<32> val, out bit<32> read_value) {
      read_value = val;
      val = hdr.ipv4.src_addr;
    }
  };

  RegisterAction<bit<32>, _, bit<32>>(store) store_dst = {
    void apply(inout bit<32> val, out bit<32> read_value) {
      read_value = val;
      val = hdr.ipv4.dst_addr;
    }
  };

  RegisterAction<bit<32>, _, bit<32>>(store) store_ports = {
    void apply(inout bit<32> val, out bit<32> read_value) {
      read_value = val;
      val = hdr.udp.src_port ++ hdr.udp.dst_port;
    }
  };

  RegisterAction<bit<32>, _, bool>(table_1_1) table_1_1_lookup = {
    void apply(inout bit<32> val, out bool read_value) {
      if (hdr.ipv4.src_addr == val) {
        read_value = true;
      } else {
        read_value = false;
      }
    }
  };

  RegisterAction<bit<32>, _, bool>(table_1_2) table_1_2_lookup = {
    void apply(inout bit<32> val, out bool read_value) {
      if (hdr.ipv4.dst_addr == val) {
        read_value = true;
      } else {
        read_value = false;
      }
    }
  };

  RegisterAction<bit<32>, _, bool>(table_1_3) table_1_3_lookup = {
    void apply(inout bit<32> val, out bool read_value) {
      bit<32> ports = hdr.udp.src_port ++ hdr.udp.dst_port;
      if (ports == val) {
        read_value = true;
      } else {
        read_value = false;
      }
    }
  };

  RegisterAction<bit<8>, _, bool>(table_1_4) table_1_4_lookup = {
    void apply(inout bit<8> val, out bool read_value) {
      if (hdr.ipv4.protocol == val) {
        read_value = true;
      } else {
        read_value = false;
      }
    }
  };

  /*RegisterAction<bit<32>, _, bool>(table_2_1) table_2_1_lookup = {*/
  /*  void apply(inout bit<32> val, out bool read_value) {*/
  /*    if (hdr.ipv4.src_addr == val) {*/
  /*      read_value = true;*/
  /*    } else {*/
  /*      read_value = false;*/
  /*    }*/
  /*  }*/
  /*};*/
  /**/
  /*RegisterAction<bit<32>, _, bool>(table_2_2) table_2_2_lookup = {*/
  /*  void apply(inout bit<32> val, out bool read_value) {*/
  /*    if (hdr.ipv4.dst_addr == val) {*/
  /*      read_value = true;*/
  /*    } else {*/
  /*      read_value = false;*/
  /*    }*/
  /*  }*/
  /*};*/
  /**/
  /*RegisterAction<bit<16>, _, bool>(table_2_3) table_2_3_lookup = {*/
  /*  void apply(inout bit<16> val, out bool read_value) {*/
  /*    if (hdr.udp.src_port == val) {*/
  /*      read_value = true;*/
  /*    } else {*/
  /*      read_value = false;*/
  /*    }*/
  /*  }*/
  /*};*/
  /**/
  /*RegisterAction<bit<16>, _, bool>(table_2_4) table_2_4_lookup = {*/
  /*  void apply(inout bit<16> val, out bool read_value) {*/
  /*    if (hdr.udp.dst_port == val) {*/
  /*      read_value = true;*/
  /*    } else {*/
  /*      read_value = false;*/
  /*    }*/
  /*  }*/
  /*};*/
  /**/
  /*RegisterAction<bit<8>, _, bool>(table_2_5) table_2_5_lookup = {*/
  /*  void apply(inout bit<8> val, out bool read_value) {*/
  /*    if (hdr.ipv4.protocol == val) {*/
  /*      read_value = true;*/
  /*    } else {*/
  /*      read_value = false;*/
  /*    }*/
  /*  }*/
  /*};*/

  RegisterAction<bit<32>, _, bit<32>>(table_1_1) table_1_1_swap = {
    void apply(inout bit<32> val, out bit<32> read_value) {
      read_value = val;
      val = ig_md.src_addr;
    }
  };

  RegisterAction<bit<32>, _, bit<32>>(table_1_2) table_1_2_swap = {
    void apply(inout bit<32> val, out bit<32> read_value) {
      read_value = val;
      val = ig_md.dst_addr;
    }
  };
  /*RegisterAction<bit<32>, _, bit<32>>(table_1_3) table_1_3_swap = {*/
  /*  void apply(inout bit<32> val, out bit<32> read_value) {*/
  /*    read_value = val;*/
  /*    val = ig_md.ports;*/
  /*  }*/
  /*};*/

  /*RegisterAction<bit<32>, _, bit<32>>(table_2_1) table_2_1_swap = {*/
  /*  void apply(inout bit<32> val, out bit<32> read_value) {*/
  /*    read_value = val;*/
  /*    val = table_1_1_entry;*/
  /*  }*/
  /*};*/
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

  action resubmit_hdr() {
    ig_md.resubmit_md.src_addr = hdr.ipv4.src_addr;
    ig_md.resubmit_md.dst_addr = hdr.ipv4.dst_addr;
    ig_intr_dprsr_md.resubmit_type = DPRSR_RESUB;
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


  apply { 
    get_hash1();
    get_hash2();
    if (ig_intr_md.resubmit_flag == 0) {
      bool found_t_1_1 = table_1_1_lookup.execute(ig_md.idx1); 
      bool found_t_1_2 = table_1_2_lookup.execute(ig_md.idx1); 
      /*bool found_t_1_3 = table_1_3_lookup.execute(ig_md.idx1); */
      /*bool found_t_1_4 = table_1_4_lookup.execute(ig_md.idx1); */
      if (found_t_1_1 && found_t_1_2) {
        ig_md.found = true;
      /*} else {*/
      /*  bool found_t_2_1 = table_2_1_lookup.execute(ig_md.idx2); */
      /*  bool found_t_2_2 = table_2_2_lookup.execute(ig_md.idx2); */
      /*  bool found_t_2_3 = table_2_3_lookup.execute(ig_md.idx2); */
      /*  bool found_t_2_4 = table_2_4_lookup.execute(ig_md.idx2); */
      /*  bool found_t_2_5 = table_2_5_lookup.execute(ig_md.idx2); */
      /*  if (found_t_2_1 && found_t_2_2 && found_t_2_3 && found_t_2_4 && found_t_2_5) {*/
      /*    ig_md.found = true;*/
      /*  }*/
      } else {
        store_src.execute(0);
        store_dst.execute(1);
        store_ports.execute(2);
      }
      // Resubmit packet
      resub.apply();
    } else {
      ig_md.src_addr = store_src.execute(0);
      ig_md.dst_addr = store_dst.execute(1);
      /*ig_md.ports = store_ports.execute(2);*/
      table_1_1_swap.execute(ig_md.idx1);
      table_1_2_swap.execute(ig_md.idx1);
      /*table_1_3_swap.execute(ig_md.idx1);*/
    }

    ig_intr_tm_md.ucast_egress_port = ig_intr_md.ingress_port;
    ig_intr_tm_md.bypass_egress = 1w1;
  }
}

Pipeline(SwitchIngressParser(), SwitchIngress(), SwitchIngressDeparser(),
         EmptyEgressParser(), EmptyEgress(), EmptyEgressDeparser()) pipe;

Switch(pipe) main;
