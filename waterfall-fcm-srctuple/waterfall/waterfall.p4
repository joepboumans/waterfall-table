#ifndef _WATERFALL_
#define _WATERFALL_

#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif

#include "../common/headers.p4"
#include "../common/util.p4"

// Waterfall defines
#define FLOW_ID_BIT_WIDTH 32 // SrcAddr
#define HFLOW_ID_BIT_WIDTH 16 // SrcAddr / 2
#define IDX_BIT_WIDTH 16
#define WATERFALL_WIDTH 65535 // 2 ^ IDX_BIT_WIDTH - 1 = WATERFALL_WIDTH

const bit<8> RESUB = 3;
const bit<3> DPRSR_RESUB = 3;
const bit<3> DIGEST = 5;

header resubmit_md_t {
  bit<8> type;
  bit<FLOW_ID_BIT_WIDTH> remain;
}

struct port_metadata_t {
  bit<32> f1;
  bit<32> f2;
}

struct digest_t {
  bit<FLOW_ID_BIT_WIDTH> src_addr;
}

struct waterfall_metadata_t {
  port_metadata_t port_metadata;
  resubmit_md_t resubmit_md;
  bool found_hi;
  bool found_lo;
  bit<IDX_BIT_WIDTH> idx1;
  bit<IDX_BIT_WIDTH> idx2;
  bit<HFLOW_ID_BIT_WIDTH> remain1_hi;
  bit<HFLOW_ID_BIT_WIDTH> remain1_lo;
  bit<HFLOW_ID_BIT_WIDTH> remain2_hi;
  bit<HFLOW_ID_BIT_WIDTH> remain2_lo;
}

// ---------------------------------------------------------------------------
// Waterfall Ingress Parser
// ---------------------------------------------------------------------------
parser WaterfallIngressParser(packet_in pkt, out header_t hdr, out waterfall_metadata_t ig_md,
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
// Waterfall Ingress 
// ---------------------------------------------------------------------------
control WaterfallIngress(inout header_t hdr, inout waterfall_metadata_t ig_md,
              in ingress_intrinsic_metadata_t ig_intr_md,
              in ingress_intrinsic_metadata_from_parser_t ig_intr_prsr_md,
              inout ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md,
              inout ingress_intrinsic_metadata_for_tm_t ig_intr_tm_md) {

  CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial
                         true,          // reversed
                         false,         // use msb?
                         false,         // extended?
                         32w0xFFFFFFFF, // initial shift register value
                         32w0xFFFFFFFF  // result xor
                         ) CRC32_1;
  Hash<bit<IDX_BIT_WIDTH>>(HashAlgorithm_t.CUSTOM, CRC32_1) hash1;

  CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial
                         true,          // reversed
                         false,         // use msb?
                         false,         // extended?
                         32w0xFFFFFFF0, // initial shift register value
                         32w0xFFFFFFFF  // result xor
                         ) CRC32_2;
  Hash<bit<IDX_BIT_WIDTH>>(HashAlgorithm_t.CUSTOM, CRC32_2) hash2;

  /*CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial*/
  /*                       true,          // reversed*/
  /*                       false,         // use msb?*/
  /*                       false,         // extended?*/
  /*                       32w0xFFFFFF00, // initial shift register value*/
  /*                       32w0xFFFFFFFF  // result xor*/
  /*                       ) CRC32_3;*/
  /*Hash<bit<IDX_BIT_WIDTH>>(HashAlgorithm_t.CUSTOM, CRC32_3) hash3;*/
  /**/
  /*CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial*/
  /*                       true,          // reversed*/
  /*                       false,         // use msb?*/
  /*                       false,         // extended? 32w0xFFFFF000, // initial shift register value*/
  /*                       32w0xFFFFF000, // initial shift register value*/
  /*                       32w0xFFFFFFFF  // result xor*/
  /*                       ) CRC32_4;*/
  /*Hash<bit<IDX_BIT_WIDTH>>(HashAlgorithm_t.CUSTOM, CRC32_4) hash4;*/

  Register<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_lo; 
  Register<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_hi; 
  Register<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_lo; 
  Register<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_hi; 

  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bool>(table_1_lo) table_1_lo_lookup = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bool read_value) {
      if (hdr.ipv4.src_addr[15:0] == val) {
        read_value = true;
      }
    }
  };

  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bool>(table_1_hi) table_1_hi_lookup = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bool read_value) {
      if (hdr.ipv4.src_addr[31:16] == val) {
        read_value = true;
      }
    }
  };

  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bool>(table_2_lo) table_2_lo_lookup = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bool read_value) {
      if (hdr.ipv4.src_addr[15:0] == val) {
        read_value = true;
      }
    }
  };

  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bool>(table_2_hi) table_2_hi_lookup = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bool read_value) {
      if (hdr.ipv4.src_addr[31:16] == val) {
        read_value = true;
      }
    }
  };

  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<HFLOW_ID_BIT_WIDTH>>(table_1_lo) table_1_lo_swap = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bit<HFLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.resubmit_md.remain[15:0];
    }
  };

  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<HFLOW_ID_BIT_WIDTH>>(table_1_hi) table_1_hi_swap = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bit<HFLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.resubmit_md.remain[31:16];
    }
  };


  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<HFLOW_ID_BIT_WIDTH>>(table_2_lo) table_2_lo_swap = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bit<HFLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain1_lo;
    }
  };

  RegisterAction<bit<HFLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<HFLOW_ID_BIT_WIDTH>>(table_2_hi) table_2_hi_swap = {
    void apply(inout bit<HFLOW_ID_BIT_WIDTH> val, out bit<HFLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain1_hi;
    }
  };

  action resubmit_hdr() {
    ig_intr_dprsr_md.resubmit_type = DPRSR_RESUB;
    ig_md.resubmit_md.type = RESUB;
    ig_md.resubmit_md.remain = hdr.ipv4.src_addr;
    ig_intr_dprsr_md.digest_type = DIGEST;
  }

  action no_action() {
  }

  table resub {
    key = {
      ig_md.found_hi : exact;
      ig_md.found_lo : exact;
      ig_intr_md.resubmit_flag : exact;
    }
    actions = {
      resubmit_hdr;
      no_action;
    }
    default_action = no_action;
    size = 8;
  }

  action do_swap1_lo() {
    ig_md.remain1_lo = table_1_lo_swap.execute(ig_md.idx1);
  }

  action lookup1_lo(){
    ig_md.found_lo = table_1_lo_lookup.execute(ig_md.idx1);
    ig_md.remain1_lo = hdr.ipv4.src_addr[15:0];
  }

  action do_swap1_hi() {
    ig_md.remain1_hi = table_1_hi_swap.execute(ig_md.idx1);
  }

  action lookup1_hi(){
    ig_md.found_hi = table_1_hi_lookup.execute(ig_md.idx1);
    ig_md.remain1_hi = hdr.ipv4.src_addr[31:16];
  }

  table swap1_lo {
    key = {
      ig_intr_md.resubmit_flag : exact;
    }
    actions = {
      do_swap1_lo;
      lookup1_lo;
      no_action;
    }
    default_action = no_action;
    size = 4;
  }

  table swap1_hi {
    key = {
      ig_intr_md.resubmit_flag : exact;
    }
    actions = {
      do_swap1_hi;
      lookup1_hi;
      no_action;
    }
    default_action = no_action;
    size = 4;
  }


  action do_swap2_lo() {
    ig_md.remain2_lo = table_2_lo_swap.execute(ig_md.idx2);
  }

  action lookup2_lo(){
    ig_md.found_lo = table_2_lo_lookup.execute(ig_md.idx2);
    ig_md.remain2_lo = hdr.ipv4.src_addr[15:0];
  }

  action do_swap2_hi() {
    ig_md.remain2_hi = table_2_hi_swap.execute(ig_md.idx2);
  }

  action lookup2_hi(){
    ig_md.found_hi = table_2_hi_lookup.execute(ig_md.idx2);
    ig_md.remain2_hi = hdr.ipv4.src_addr[31:16];
  }

  table swap2_lo {
    key = {
      ig_intr_md.resubmit_flag : exact;
      ig_md.found_hi : exact;
      ig_md.found_lo : exact;
    }
    actions = {
      do_swap2_lo;
      lookup2_lo;
      no_action;
    }
    default_action = no_action;
    size = 8;
  }

  table swap2_hi {
    key = {
      ig_intr_md.resubmit_flag : exact;
      ig_md.found_hi : exact;
      ig_md.found_lo : exact;
    }
    actions = {
      do_swap2_hi;
      lookup2_hi;
      no_action;
    }
    default_action = no_action;
    size = 8;
  }


  action hit(PortId_t dst_port) {
    ig_intr_tm_md.ucast_egress_port = dst_port;
    ig_md.found_hi = false;
    ig_md.found_lo = false;
    ig_intr_dprsr_md.drop_ctl = 0x0;
  }
  action drop() { ig_intr_dprsr_md.drop_ctl = 0x1; }

  //@stage(0)
  table forward {
    key = {
      ig_intr_md.ingress_port: exact;
    }
    actions = {
      hit;
      drop;
      no_action;
    }
    size = 512;
    default_action = drop;
  }

  apply { 
    forward.apply();
    ig_md.idx1 = hash1.get({hdr.ipv4.src_addr});
    swap1_hi.apply();
    swap1_lo.apply();
    ig_md.idx2 = hash2.get({ig_md.remain1_hi, ig_md.remain1_lo});
    swap2_hi.apply();
    swap2_lo.apply();

    resub.apply();
  }
}

// ---------------------------------------------------------------------------
// Waterfall Ingress Deparser
// ---------------------------------------------------------------------------
control WaterfallIngressDeparser( packet_out pkt, inout header_t hdr, in waterfall_metadata_t ig_md,
    in ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md) {

  Digest<digest_t>() digest;
  Resubmit() resubmit;

  apply {
    if (ig_intr_dprsr_md.digest_type == DIGEST) {
      digest.pack({hdr.ipv4.src_addr});
    }
    if (ig_intr_dprsr_md.resubmit_type == DPRSR_RESUB) {
      resubmit.emit(ig_md.resubmit_md);
    }
    pkt.emit(hdr);
  }
}

#endif
