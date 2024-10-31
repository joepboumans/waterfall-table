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

#include "waterfall/waterfall.p4"


// FCM Sketch defines
#define SKETCH_W1 0x80000 // 8 bits, width at layer 1, 2^19 = 524288
#define SKETCH_W2 0x10000 // 16 bits, width at layer 2, 2^16 = 65536
#define SKETCH_W3 0x2000 // 32 bits, width at layer 3, 2^13 = 8192 

#define ADD_LEVEL1 0x000000ff // 2^8 - 2 + 1 (property of SALU)
#define ADD_LEVEL2 0x000100fd // (2^8 - 2) + (2^16 - 2) + 1 (property of SALU)

Pipeline(WaterfallIngressParser(), WaterfallIngress(), WaterfallIngressDeparser(),
         EmptyEgressParser(), EmptyEgress(), EmptyEgressDeparser()) pipe;

Switch(pipe) main;
