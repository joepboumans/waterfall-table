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
#define FLOW_ID_BIT_WIDTH 16 // SrcAddr / 2
#define IDX_BIT_WIDTH 16
#define WATERFALL_WIDTH 65535 // 2 ^ IDX_BIT_WIDTH - 1 = WATERFALL_WIDTH

const bit<8> RESUB = 3;
const bit<3> DPRSR_RESUB = 3;
const bit<3> DIGEST = 5;

header resubmit_md_t {
  bit<8> type;
  bit<FLOW_ID_BIT_WIDTH> remain_hi;
  bit<FLOW_ID_BIT_WIDTH> remain_lo;
}

struct port_metadata_t {
  bit<32> f1;
  bit<32> f2;
}

struct digest_t {
  bit<32> src_addr;
}

struct waterfall_metadata_t {
  port_metadata_t port_metadata;
  resubmit_md_t resubmit_md;
  bit<4> found_hi;
  bit<4> found_lo;
  bit<IDX_BIT_WIDTH> idx1;
  bit<IDX_BIT_WIDTH> idx2;
  bit<IDX_BIT_WIDTH> idx3;
  bit<IDX_BIT_WIDTH> idx4;
  bit<FLOW_ID_BIT_WIDTH> remain1_hi;
  bit<FLOW_ID_BIT_WIDTH> remain1_lo;
  bit<FLOW_ID_BIT_WIDTH> remain2_hi;
  bit<FLOW_ID_BIT_WIDTH> remain2_lo;
  bit<FLOW_ID_BIT_WIDTH> remain3_hi;
  bit<FLOW_ID_BIT_WIDTH> remain3_lo;
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

  CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial
                         true,          // reversed
                         false,         // use msb?
                         false,         // extended?
                         32w0xFFFFFF00, // initial shift register value
                         32w0xFFFFFFFF  // result xor
                         ) CRC32_3;
  Hash<bit<IDX_BIT_WIDTH>>(HashAlgorithm_t.CUSTOM, CRC32_3) hash3;

  CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial
                         true,          // reversed
                         false,         // use msb?
                         false,         // extended? 32w0xFFFFF000, // initial shift register value
                         32w0xFFFFF000, // initial shift register value
                         32w0xFFFFFFFF  // result xor
                         ) CRC32_4;
  Hash<bit<IDX_BIT_WIDTH>>(HashAlgorithm_t.CUSTOM, CRC32_4) hash4;

  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_hi; 
  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_1_lo; 
  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_hi; 
  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_2_lo; 
  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_3_hi; 
  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_3_lo; 
  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_4_hi; 
  Register<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>>(WATERFALL_WIDTH, 0) table_4_lo; 

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_1_hi) table_1_hi_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[31:16] == val) {
        read_value = 0x1;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_1_lo) table_1_lo_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[15:0] == val) {
        read_value = 0x1;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_2_hi) table_2_hi_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[31:16] == val) {
        read_value = 0x2;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_2_lo) table_2_lo_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[15:0] == val) {
        read_value = 0x2;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_3_hi) table_3_hi_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[31:16] == val) {
        read_value = 0x3;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_3_lo) table_3_lo_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[15:0] == val) {
        read_value = 0x3;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_4_hi) table_4_hi_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[31:16] == val) {
        read_value = 0x4;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<4>>(table_4_lo) table_4_lo_lookup = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<4> read_value) {
      if (hdr.ipv4.src_addr[15:0] == val) {
        read_value = 0x4;
      }
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_1_hi) table_1_hi_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.resubmit_md.remain_hi;
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_1_lo) table_1_lo_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.resubmit_md.remain_lo;
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_2_hi) table_2_hi_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain1_hi;
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_2_lo) table_2_lo_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain1_lo;
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_3_hi) table_3_hi_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain2_hi;
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_3_lo) table_3_lo_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain2_lo;
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_4_hi) table_4_hi_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain3_hi;
    }
  };

  RegisterAction<bit<FLOW_ID_BIT_WIDTH>, bit<IDX_BIT_WIDTH>, bit<FLOW_ID_BIT_WIDTH>>(table_4_lo) table_4_lo_swap = {
    void apply(inout bit<FLOW_ID_BIT_WIDTH> val, out bit<FLOW_ID_BIT_WIDTH> read_value) {
      read_value = val;
      val = ig_md.remain3_lo;
    }
  };

  action resubmit_hdr() {
    ig_md.resubmit_md.remain_hi = hdr.ipv4.src_addr[31:16];
    ig_md.resubmit_md.remain_lo = hdr.ipv4.src_addr[15:0];
    ig_md.resubmit_md.type = RESUB;
    ig_intr_dprsr_md.resubmit_type = DPRSR_RESUB;
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
    size = 512;
  }

  action do_swap1_lo() {
    ig_md.remain1_lo = table_1_lo_swap.execute(hash1.get({hdr.ipv4.src_addr}));
  }

  action lookup1_lo(){
    ig_md.found_lo = table_1_lo_lookup.execute(hash1.get({hdr.ipv4.src_addr}));
    ig_md.remain1_lo = hdr.ipv4.src_addr[15:0];
  }

  action do_swap1_hi() {
    ig_md.remain1_hi = table_1_hi_swap.execute(hash1.get({hdr.ipv4.src_addr}));
  }

  action lookup1_hi(){
    ig_md.found_hi = table_1_hi_lookup.execute(hash1.get({hdr.ipv4.src_addr}));
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
    size = 512;
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
    size = 512;
  }


  action do_swap2_lo() {
    ig_md.remain2_lo = table_2_lo_swap.execute(hash2.get({ig_md.remain1_hi, ig_md.remain1_lo}));
  }

  action lookup2_lo(){
    ig_md.found_lo = table_2_lo_lookup.execute(hash2.get({ig_md.remain1_hi, ig_md.remain1_lo}));
    ig_md.remain2_lo = hdr.ipv4.src_addr[15:0];
  }

  action do_swap2_hi() {
    ig_md.remain2_hi = table_2_hi_swap.execute(hash2.get({ig_md.remain1_hi, ig_md.remain1_lo}));
  }

  action lookup2_hi(){
    ig_md.found_hi = table_2_hi_lookup.execute(hash2.get({ig_md.remain1_hi, ig_md.remain1_lo}));
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
    size = 512;
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
    size = 512;
  }


  action do_swap3_lo() {
    ig_md.remain3_lo = table_3_lo_swap.execute(hash3.get({ig_md.remain2_hi, ig_md.remain2_lo}));
  }

  action lookup3_lo(){
    ig_md.found_lo = table_3_lo_lookup.execute(hash3.get({ig_md.remain2_hi, ig_md.remain2_lo}));
    ig_md.remain3_lo = hdr.ipv4.src_addr[15:0];
  }

  action do_swap3_hi() {
    ig_md.remain3_hi = table_3_hi_swap.execute(hash3.get({ig_md.remain2_hi, ig_md.remain2_lo}));
  }

  action lookup3_hi(){
    ig_md.found_hi = table_3_hi_lookup.execute(hash3.get({ig_md.remain2_hi, ig_md.remain2_lo}));
    ig_md.remain3_hi = hdr.ipv4.src_addr[31:16];

  }

  table swap3_lo {
    key = {
      ig_intr_md.resubmit_flag : exact;
      ig_md.found_hi : exact;
      ig_md.found_lo : exact;
    }
    actions = {
      do_swap3_lo;
      lookup3_lo;
      no_action;
    }
    default_action = no_action;
    size = 512;
  }

  table swap3_hi {
    key = {
      ig_intr_md.resubmit_flag : exact;
      ig_md.found_hi : exact;
      ig_md.found_lo : exact;
    }
    actions = {
      do_swap3_hi;
      lookup3_hi;
      no_action;
    }
    default_action = no_action;
    size = 512;
  }

  action do_swap4_lo() {
    table_4_lo_swap.execute(hash4.get({ig_md.remain3_hi, ig_md.remain3_lo}));
  }

  action lookup4_lo(){
    ig_md.found_lo = table_4_lo_lookup.execute(hash4.get({ig_md.remain3_hi, ig_md.remain3_lo}));
  }

  action do_swap4_hi() {
    table_4_hi_swap.execute(hash4.get({ig_md.remain3_hi, ig_md.remain3_lo}));
  }

  action lookup4_hi(){
    ig_md.found_hi = table_4_hi_lookup.execute(hash4.get({ig_md.remain3_hi, ig_md.remain3_lo}));
  }

  table swap4_lo {
    key = {
      ig_intr_md.resubmit_flag : exact;
      ig_md.found_hi : exact;
      ig_md.found_lo : exact;
    }
    actions = {
      do_swap4_lo;
      lookup4_lo;
      no_action;
    }
    default_action = no_action;
    size = 512;
  }

  table swap4_hi {
    key = {
      ig_intr_md.resubmit_flag : exact;
      ig_md.found_hi : exact;
      ig_md.found_lo : exact;
    }
    actions = {
      do_swap4_hi;
      lookup4_hi;
      no_action;
    }
    default_action = no_action;
    size = 512;
  }

  action hit(PortId_t dst_port) {
    ig_intr_tm_md.ucast_egress_port = dst_port;
    ig_md.found_hi = 0x0;
    ig_md.found_lo = 0x0;
    ig_intr_dprsr_md.drop_ctl = 0x0;
    ig_intr_dprsr_md.resubmit_type = 0;
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

    swap1_hi.apply();
    swap1_lo.apply();

    swap2_lo.apply();
    swap2_hi.apply();

    swap3_hi.apply();
    swap3_lo.apply();

    swap4_hi.apply();
    swap4_lo.apply();

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
