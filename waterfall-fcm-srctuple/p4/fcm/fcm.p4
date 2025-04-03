#ifndef _FCM_
#define _FCM_

/* -*- P4_16 -*- */

// ------------------------------------------------------------
// FCM-Sketch P4-16 Implementation
//
//	- Flow identification
// You can define our flow identification (currrnetly, we use srcIP)
// in action 'fcm_action_calc_hash_d1' and 'fcm_action_calc_hash_d2'.  
//
//	- Data Plane Queries
// 		- Flow Size estimation is supported by bit<32> flow_size.
//			Of course, you can in turn classify Heavy hitters and Heavy changes.
//		- Cardinality is supported by bit<32> cardinality.
//			We reduced the TCAM entries via sensitivity analysis of LC estimator.
//			It is a kind of adaptive spacing between TCAM entries with additional error.
//			Ideally, we can reduce 100X of TCAM entries with only 0.3 % additional error.
// 	- Control Plane Queries : Read the bottom of "test_fcm.py".
//		- Flow Size distribution
//		- Entropy
//
// ------------------------------------------------------------



#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif

#include "../common/headers.p4"
#include "../common/util.p4"

#define SKETCH_W1 0x80000 // 8 bits, width at layer 1, 2^19 = 524288
#define SKETCH_W2 0x10000 // 16 bits, width at layer 2, 2^16 = 65536
#define SKETCH_W3 0x2000 // 32 bits, width at layer 3, 2^13 = 8192 

#define ADD_LEVEL1 0x000000ff // 2^8 - 2 + 1 (property of SALU)
#define ADD_LEVEL2 0x000100fd // (2^8 - 2) + (2^16 - 2) + 1 (property of SALU)


// ---------------------------------------------------------------------------
// Metadata fields
// ---------------------------------------------------------------------------

// metadata fields for fcm
struct fcm_metadata_t {
    bit<32> hash_meta_d1;
    bit<32> hash_meta_d2;

    bit<32> result_d1;
    bit<32> result_d2;
    bit<32> increment_occupied;
}

// ---------------------------------------------------------------------------
// FCM Egress parser
// ---------------------------------------------------------------------------

parser FcmEgressParser(
        packet_in pkt,
        out header_t hdr,
        out fcm_metadata_t eg_md,
        out egress_intrinsic_metadata_t eg_intr_md) {

    TofinoEgressParser() tofino_parser;

    state start {
        tofino_parser.apply(pkt, eg_intr_md);
        // initialize metadata
    	eg_md.result_d1 = 0;
    	eg_md.result_d2 = 0;
    	eg_md.increment_occupied = 0;

      transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select (hdr.ethernet.ether_type) {
            ETHERTYPE_IPV4 : parse_ipv4;
            default : reject;
        }
    }
    
    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            IP_PROTOCOLS_TCP : parse_tcp;
            IP_PROTOCOLS_UDP : parse_udp;
            default : accept;
        }
    }
    
    state parse_tcp {
        pkt.extract(hdr.tcp);
        transition select(hdr.ipv4.total_len) {
            default : accept;
        }
    }
    
    state parse_udp {
        pkt.extract(hdr.udp);
        transition select(hdr.udp.dst_port) {
            default: accept;
        }
    }
}


// ---------------------------------------------------------------------------
// FCM logic control block
// ---------------------------------------------------------------------------
control FCMSketch (
	inout header_t hdr,
	out fcm_metadata_t fcm_mdata,
	out bit<32> flow_size) {

	// +++++++++++++++++++ 
	//	hashings & hash action
	// +++++++++++++++++++

    CRCPolynomial<bit<32>>(32w0x04C11DB7, // polynomial
                           true,          // reversed
                           false,         // use msb?
                           false,         // extended?
                           32w0xFFFFFFFF, // initial shift register value
                           32w0xFFFFFFFF  // result xor
                           ) CRC32;
    Hash<bit<32>>(HashAlgorithm_t.CUSTOM, CRC32) hash_d1;

	// Hash<bit<32>>(HashAlgorithm_t.CRC32) hash_d1;


    CRCPolynomial<bit<32>>(32w0x04C11DB7, 
                           true, 
                           false, 
                           false, 
                           32w0xFFFFFFF0, // initial shift register value
                           32w0xFFFFFFFF  // result xor
                           ) CRC32_MPEG;
    Hash<bit<32>>(HashAlgorithm_t.CUSTOM, CRC32_MPEG) hash_d2;

    
	// +++++++++++++++++++ 
	//	registers
	// +++++++++++++++++++

	Register<bit<8>, bit<19>>(SKETCH_W1) sketch_reg_l1_d1;
	Register<bit<16>, bit<16>>(SKETCH_W2) sketch_reg_l2_d1;
	Register<bit<32>, bit<13>>(SKETCH_W3) sketch_reg_l3_d1;

	Register<bit<8>, bit<19>>(SKETCH_W1) sketch_reg_l1_d2;
	Register<bit<16>, bit<16>>(SKETCH_W2) sketch_reg_l2_d2;
	Register<bit<32>, bit<13>>(SKETCH_W3) sketch_reg_l3_d2;

	// total number of empty registers for all trees
	Register<bit<32>, _>(1) reg_num_empty;

	// +++++++++++++++++++ 
	//	register actions
	// +++++++++++++++++++

	// level 1, depth 1
	RegisterAction<bit<8>, bit<19>, bit<32>>(sketch_reg_l1_d1) increment_l1_d1 = {
		void apply(inout bit<8> value, out bit<32> result) {
			value = value |+| 1;
			result = (bit<32>)value; // return level 1 value (255 -> count 254)
		}
	};
	// level 2, depth 1, only when level 1 output is 255
	RegisterAction<bit<16>, bit<16>, bit<32>>(sketch_reg_l2_d1) increment_l2_d1 = {
		void apply(inout bit<16> value, out bit<32> result) {
			value = value |+| 1;
			result = (bit<32>)value; // return level 1 + 2
		}
	};
	// level 3, depth 1, only when level 2 output is 65789
	RegisterAction<bit<32>, bit<13>, bit<32>>(sketch_reg_l3_d1) increment_l3_d1 = {
		void apply(inout bit<32> value, out bit<32> result) {
			value = value |+| 1;
			result = value + ADD_LEVEL2; // return level 1 + 2 + 3
			
		}
	};

	// level 1, depth 2
	RegisterAction<bit<8>, bit<19>, bit<32>>(sketch_reg_l1_d2) increment_l1_d2 = {
		void apply(inout bit<8> value, out bit<32> result) {
			value = value |+| 1;
			result = (bit<32>)value; // return level 1 value (255 -> count 254)
		}
	};
	// level 2, depth 2, only when level 1 output is 255
	RegisterAction<bit<16>, bit<16>, bit<32>>(sketch_reg_l2_d2) increment_l2_d2 = {
		void apply(inout bit<16> value, out bit<32> result) {
			value = value |+| 1;
			result = (bit<32>)value; // return level 1 + 2
		}
	};
	// level 3, depth 2, only when level 2 output is 65789
	RegisterAction<bit<32>, bit<13>, bit<32>>(sketch_reg_l3_d2) increment_l3_d2 = {
		void apply(inout bit<32> value, out bit<32> result) {
			value = value |+| 1;
			result = value + ADD_LEVEL2; // return level 1 + 2 + 3
			
		}
	};

	// increment number of empty register value for cardinality
	RegisterAction<bit<32>, _, bit<32>>(reg_num_empty) increment_occupied_reg = {
		void apply(inout bit<32> value, out bit<32> result) {
			result = value + fcm_mdata.increment_occupied;
			value = value + fcm_mdata.increment_occupied;
		}
	};


	// +++++++++++++++++++ 
	//	actions
	// +++++++++++++++++++
  action fcm_hash_d1() {
    fcm_mdata.hash_meta_d1 = hash_d1.get({ hdr.ipv4.src_addr});
  }

  action fcm_hash_d2() {
    fcm_mdata.hash_meta_d2 = hash_d2.get({ hdr.ipv4.src_addr});
  }

	// action for level 1, depth 1, you can re-define the flow key identification
	action fcm_action_l1_d1() {
		fcm_mdata.result_d1 = increment_l1_d1.execute(fcm_mdata.hash_meta_d1[18:0]);
	}
	// action for level 2, depth 1
	action fcm_action_l2_d1() {
    fcm_mdata.result_d1 = increment_l2_d1.execute(fcm_mdata.hash_meta_d1[18:3]) + ADD_LEVEL1;
	}
	// action for level 3, depth 1
	action fcm_action_l3_d1() {
		fcm_mdata.result_d1 = increment_l3_d1.execute(fcm_mdata.hash_meta_d1[18:6]);
	}

	// action for level 1, depth 2, you can re-define the flow key identification
	action fcm_action_l1_d2() {
		fcm_mdata.result_d2 = increment_l1_d2.execute(fcm_mdata.hash_meta_d2[18:0]);
	}
	// action for level 2, depth 2
	action fcm_action_l2_d2() {
    fcm_mdata.result_d2 = increment_l2_d2.execute(fcm_mdata.hash_meta_d2[18:3]) + ADD_LEVEL1;
	}
	// action for level 3, depth 2
	action fcm_action_l3_d2() {
		fcm_mdata.result_d2 = increment_l3_d2.execute(fcm_mdata.hash_meta_d2[18:6]);
	}

	// +++++++++++++++++++ 
	//	tables
	// +++++++++++++++++++

	// if level 1 is full, move to level 2.
	table tb_fcm_l1_to_l2_d1 {
		key = {
			fcm_mdata.result_d1 : exact;
		}
		actions = {
			fcm_action_l2_d1;
			@defaultonly NoAction;
		}
		const default_action = NoAction();
		const entries = {
			32w255: fcm_action_l2_d1();
		}
		size = 2;
	}

	// if level 2 is full, move to level 3.
	table tb_fcm_l2_to_l3_d1 {
		key = {
			fcm_mdata.result_d1 : exact;
		}
		actions = {
			fcm_action_l3_d1;
			@defaultonly NoAction;
		}
		const default_action = NoAction();
		const entries = {
			32w65789: fcm_action_l3_d1();
		}
		size = 2;
	}

	// if level 1 is full, move to level 2.
	table tb_fcm_l1_to_l2_d2 {
		key = {
			fcm_mdata.result_d2 : exact;
		}
		actions = {
			fcm_action_l2_d2;
			@defaultonly NoAction;
		}
		const default_action = NoAction();
		const entries = {
			32w255: fcm_action_l2_d2();
		}
		size = 2;
	}

	// if level 2 is full, move to level 3.
	table tb_fcm_l2_to_l3_d2 {
		key = {
			fcm_mdata.result_d2 : exact;
		}
		actions = {
			fcm_action_l3_d2;
			@defaultonly NoAction;
		}
		const default_action = NoAction();
		const entries = {
			32w65789: fcm_action_l3_d2();
		}
		size = 2;
	}

	// +++++++++++++++++++ 
	//	apply
	// +++++++++++++++++++
	apply {
    fcm_hash_d1();
    fcm_hash_d2();

		fcm_action_l1_d1();			// increment level 1, depth 1
		fcm_action_l1_d2();			// increment level 1, depth 2
		/* increment the number of occupied leaf nodes */
		/*tb_fcm_increment_occupied.apply(); */
		/*fcm_action_increment_cardreg(); */
		/*tb_fcm_cardinality.apply(); // calculate cardinality estimate*/
		tb_fcm_l1_to_l2_d1.apply(); // conditional increment level 2, depth 1
		tb_fcm_l1_to_l2_d2.apply(); // conditional increment level 2, depth 2
		tb_fcm_l2_to_l3_d1.apply(); // conditional increment level 3, depth 1
		tb_fcm_l2_to_l3_d2.apply(); // conditional increment level 3, depth 2

		/* Take minimum for final count-query. */
		flow_size = fcm_mdata.result_d1 > fcm_mdata.result_d2 ? fcm_mdata.result_d2 : fcm_mdata.result_d1;
	}
}


control FcmEgress(
        inout header_t hdr,
        inout fcm_metadata_t eg_md,
        in egress_intrinsic_metadata_t eg_intr_md,
        in egress_intrinsic_metadata_from_parser_t eg_prsr_md,
        inout egress_intrinsic_metadata_for_deparser_t eg_dprsr_md,
        inout egress_intrinsic_metadata_for_output_port_t eg_oport_md) {


		/*** temp ***/
		// increment when packet comes in
		Register<bit<32>, _>(1, 0) num_pkt;
		RegisterAction<bit<32>, _, bit<32>>(num_pkt) increment_pkt = {
			void apply(inout bit<32> value, out bit<32> result) {
				value = value |+| 1;
				result = value;
			}
		};

		action count_pkt() { 
			increment_pkt.execute(0); 
		}
		/*** temp ***/


		FCMSketch() fcmsketch;
		apply {
			bit<32> flow_size; // local variable for final query
			count_pkt(); // temp
			fcmsketch.apply(hdr, 
							eg_md, 
							flow_size);
      eg_dprsr_md.drop_ctl = 0x0;
		}
}

// ---------------------------------------------------------------------------
// FCM Egress Deparser
// ---------------------------------------------------------------------------
control FcmEgressDeparser(
        packet_out pkt,
        inout header_t hdr,
        in fcm_metadata_t eg_md,
        in egress_intrinsic_metadata_for_deparser_t eg_dprsr_md) {

    apply {
        pkt.emit(hdr);
    }
}

#endif // !_FCM_
