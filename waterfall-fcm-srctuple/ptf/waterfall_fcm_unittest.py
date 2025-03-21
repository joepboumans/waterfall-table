#!/usr/bin/python3
import logging
from collections import namedtuple
from math import radians
import random
import time
import re

from ptf import config
import ptf.testutils as testutils
from p4testutils.misc_utils import *
from bfruntime_client_base_tests import BfRuntimeTest
import bfrt_grpc.bfruntime_pb2 as bfruntime_pb2
import bfrt_grpc.client as gc

from fcm_utils import *
from utils import *

from EM_ctypes import EM_WFCM

DEBUGGING = False               # debugging? (True -> yes)
FROM_HW = True                  # From Tofino hardware model, must be True.
SKETCH_W1 = 524288              # 8-bit, level 1
SKETCH_W2 = 65536               # 16-bit, level 2
SKETCH_W3 = 8192                # 32-bit, level 3
ADD_LEVEL1 = 255                # 2^8 -2 + 1 (actual count is 254)
ADD_LEVEL2 = 65789              # (2^8 - 2) + (2^16 - 2) + 1 (actual count is 65788)

swports = get_sw_ports()
hwports = { 132:148, 148:132, 140:156, 156:140}
project_name = 'waterfall_fcm'
logger = logging.getLogger(project_name)

if not len(logger.handlers):
    sh = logging.StreamHandler()
    formatter = logging.Formatter('[%(levelname)s - %(name)s - %(funcName)s]: %(message)s')
    sh.setFormatter(formatter)
    sh.setLevel(logging.INFO)
    logger.addHandler(sh)

class WaterfallFcmUnitTests(BfRuntimeTest):
    # Input a number of entries and verify if the resub and digest is working
    def setUp(self):
        return
        logger.info("Starting setup")
        client_id = 0
        BfRuntimeTest.setUp(self, client_id)
        logger.info("\tfinished BfRuntimeSetup")
        self.target = gc.Target(device_id=0, pipe_id=0xffff)

        self.setupWaterfall()
        self.resetWaterfall()
        self.setupWaterfall()

        logger.info("Finished setup")

    def setupWaterfall(self):
        logger.info("Setting up tables...")
        self.bfrt_info = self.interface.bfrt_info_get(project_name)

        # Setup forwarding tables
        self.forward = self.bfrt_info.table_get("forward")

        # Get resubmission and port metadata tables
        self.port_meta = self.bfrt_info.table_get("$PORT_METADATA")
        self.resub = self.bfrt_info.table_get("resub")

        # Get digest/learn filter from gRPC
        self.learn_filter = self.bfrt_info.learn_get("digest")
        self.learn_filter.info.data_field_annotation_add("src_addr", "ipv4")

        # Get Waterfall tables
        self.table_dict = {}
        table_names = ["table_1", "table_2", "table_3", "table_4"]
        for sname in table_names:
            for loc in ["hi", "lo"]:
                name = f"{sname}_{loc}"
                table = self.bfrt_info.table_get(name)
                self.table_dict.update({name : table})

        # Get swap tables
        self.swap_dict = {}
        swap_names = ["swap1", "swap2", "swap3", "swap4"]
        for sname in swap_names:
            for loc in ["hi", "lo"]:
                name = f"{sname}_{loc}"
                swap = self.bfrt_info.table_get(name)
                self.swap_dict.update({name : swap})

        # Get FCM counters
        self.fcm_l1_d1 = self.bfrt_info.table_get("sketch_reg_l1_d1")
        self.fcm_l2_d1 = self.bfrt_info.table_get("sketch_reg_l2_d1")
        self.fcm_l3_d1 = self.bfrt_info.table_get("sketch_reg_l3_d1")
        self.fcm_l1_d2 = self.bfrt_info.table_get("sketch_reg_l1_d2")
        self.fcm_l2_d2 = self.bfrt_info.table_get("sketch_reg_l2_d2")
        self.fcm_l3_d2 = self.bfrt_info.table_get("sketch_reg_l3_d2")
        self.fcm_tables = {"fcm_l1_d1" : self.fcm_l1_d1, "fcm_l2_d1" : self.fcm_l2_d1, "fcm_l3_d1" : self.fcm_l3_d1, "fcm_l1_d2" : self.fcm_l1_d2, "fcm_l2_d2" : self.fcm_l2_d2, "fcm_l3_d2" : self.fcm_l3_d2}

        # Get Pkt count register of FCM
        self.num_pkt = self.bfrt_info.table_get("num_pkt")

        self.target = gc.Target(device_id=0, pipe_id=0xffff)
        logger.info("... tables setup")

    def clearWaterfall(self):
        logger.info("Resetting switch..")
        pkt_in = testutils.simple_tcp_packet(ip_src="192.168.1.1", ip_dst="127.0.0.1", tcp_sport=80, tcp_dport=88)
        testutils.count_matched_packets_all_ports(self, pkt_in, swports)

        self.forward.entry_del(self.target)
        self.resub.entry_del(self.target)
        self.port_meta.entry_del(self.target)

        for _, table in self.table_dict.items():
            table.entry_del(self.target)

        for table in self.fcm_tables.values():
            table.entry_del(self.target)

        for _, swap in self.swap_dict.items():
            swap.entry_del(self.target)

        self.num_pkt.entry_del(self.target)
        logger.info("...cleared all tables")

    def addSwapEntry(self, table, name):
        num_loc = name.replace("swap", "")
        num = int(re.search(r'\d+', name).group())

        if num == 1:
            key = table.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0)])
            data = table.make_data([], f"WaterfallIngress.lookup{num_loc}")
            table.entry_add(self.target, [key], [data])
        else:
            for i in range(0, num + 1):
                for j in range(0, num + 1):
                    if j == i:
                        continue

                    key = table.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0), gc.KeyTuple('ig_md.found_hi', i), gc.KeyTuple('ig_md.found_lo', j)])
                    data = table.make_data([], f"WaterfallIngress.lookup{num_loc}")
                    table.entry_add(self.target, [key], [data])

        key = table.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x1)])
        data = table.make_data([], f"WaterfallIngress.do_swap{num_loc}")
        table.entry_add(self.target, [key], [data])

    def resetWaterfall(self):
        self.clearWaterfall()
        self.setupWaterfall()

    def tearDown(self):
        logger.info("Tearing down test")
        self.clearWaterfall()

        BfRuntimeTest.tearDown(self)

    def runTest(self):
        logger.info("Start testing")

        # self.testWaterfallFcm()
        # self.resetWaterfall()

    def evaluate_digest(self, num_entries):
        learn_filter = self.learn_filter
        total_recv = 0
        digest = self.interface.digest_get()

        tuples = {}
        while(digest != None):
            data_list = learn_filter.make_data_list(digest)
            total_recv += len(data_list)
            logger.info(f"Received {len(data_list)} entries from the digest with {total_recv = }")
            for data in data_list:
                data_dict = data.to_dict()
                recv_src_addr = data_dict["src_addr"]

                # logger.info(f"{recv_src_addr = } : {recv_dst_addr = } | {recv_src_port = } {recv_dst_port = } | {recv_protocol = } | {recv_remain4}")
                raw_src_addr = [int(x) for x in recv_src_addr.split('.')]
                logger.info(f"{raw_src_addr = }")
                tuple_list = raw_src_addr 
                tuple_key = ".".join([str(x) for x in tuple_list])
                tuples[tuple_key] = tuple_list

            try:
                digest = self.interface.digest_get()
            except:
                break;
        logger.info(f"Received {total_recv} of {num_entries} packets and {len(tuples)} of unique tuples")
        assert(total_recv == num_entries)
        return tuples

    def evaluate_table(self, tables, name):
        table = tables[name]
        control_name = ""
        if "fcm" in name:
            control_name = "FcmEgress.fcmsketch"
            name = name.replace("fcm", "sketch_reg")
        else:
            control_name = "WaterfallIngress"

        logger.info(f"[FCM] Load in all data from {control_name}.{name}")
        summed = 0
        nonzero_entries = 0
        data_table = table.entry_get(self.target, [], {"from_hw" : True})
        entries = []
        index = 0;
        for data, key in data_table:
            data_dict = data.to_dict()
            entry_val = data_dict[f"{control_name}.{name}.f1"][0]
            entries.append(entry_val)
            if entry_val != 0:
                summed += entry_val
                nonzero_entries += 1
                logger.info(data_dict)
                logger.info(f"{index} : {entry_val.to_bytes(2,'big')}")

            index += 1

        logger.info(f"{name} has {summed} total remainders and {nonzero_entries} entries")
        return entries


    def testWaterfallFcm(self):
        ig_port = swports[0]
        eg_port = swports[1]
        target = self.target

        forward = self.forward
        key = forward.make_key([gc.KeyTuple('ig_intr_md.ingress_port', ig_port)])
        data = forward.make_data([gc.DataTuple('dst_port', eg_port)], "WaterfallIngress.hit")
        forward.entry_add(target, [key], [data])

        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        resub = self.resub
        # Only resubmit if both are found
        for i in range(0, 5):
            for j in range(0, 5):
                if i > 0 and j > 0 and i == j:
                    key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0), gc.KeyTuple('ig_md.found_hi', i), gc.KeyTuple('ig_md.found_lo', j)])
                    data = resub.make_data([], "WaterfallIngress.no_action")
                    resub.entry_add(target, [key], [data])
                    continue

                key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0), gc.KeyTuple('ig_md.found_hi', i), gc.KeyTuple('ig_md.found_lo', j)])
                data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
                resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', True)])
        data = resub.make_data([], "WaterfallIngress.no_action")
        resub.entry_add(target, [key], [data])

        for name, table in self.swap_dict.items():
            self.addSwapEntry(table, name)

        num_entries_src = 10
        total_pkts_sends = 0
        seed = 1001
        random.seed(seed)
        src_ip_list = self.generate_random_ip_list(num_entries_src, seed)
        in_tuples = {}

        NUM_FLOWS = num_entries_src             # number of sample flows
        MAX_FLOW_SIZE = 100_000          # max size of flows
        fsd = [0] * (MAX_FLOW_SIZE + 1)

        logger.info(f"Start sending {num_entries_src } entries")
        for src_ip in src_ip_list:
            src_addr = getattr(src_ip, "ip")
            logger.info(f"Sending {src_addr}")
            src_port = random.randrange(0, 0xFFFF)
            dst_port = random.randrange(0, 0xFFFF)
            flow_size = int(min(MAX_FLOW_SIZE, max(MAX_FLOW_SIZE * abs(random.gauss(mu=0, sigma=0.0001)), 1.0)))

            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr, tcp_sport=src_port, tcp_dport=dst_port)
            testutils.send_packet(self, ig_port, pkt_in, count=flow_size)
            testutils.verify_packet(self, pkt_in, eg_port)
            total_pkts_sends += flow_size

            raw_src_addr = [int(x) for x in src_addr.split('.')]

            tuple_list = raw_src_addr 
            tuple_key = ".".join([str(x) for x in tuple_list])
            in_tuples[tuple_key] = flow_size
            fsd[flow_size] += 1

            print(f"Sent {flow_size} pkts with total {total_pkts_sends}", flush=True)

        logger.info(f"...done sending {total_pkts_sends} packets send")
        # ''' TC:3 Look for data in digest'''
        # for key, table in self.table_dict.items():
        #     self.evaluate_table(self.table_dict, key)

        tuples = self.evaluate_digest(num_entries_src)

        register_pktcount = self.num_pkt
        # check all packets are sent
        iters = 0
        logger.info("[INFO-FCM] Wait until packets are all received...")
        while True:
            register_pktcount.operations_execute(target, 'Sync')
            
            resp_pktcount = register_pktcount.entry_get(target,
                    [register_pktcount.make_key([gc.KeyTuple('$REGISTER_INDEX', 0)])],
                    {"from_hw": FROM_HW})
            data, _ = next(resp_pktcount)
            data_dict = data.to_dict()

            logger.info("[INFO-FCM] Sent : %d, Received : %d\t\t wait 1s more...", total_pkts_sends, data_dict["FcmEgress.num_pkt.f1"][0])
            if (data_dict["FcmEgress.num_pkt.f1"][0] >= (total_pkts_sends - 100 ) or iters >= 100):
                logger.info("[INFO-FCM] Found all packets, continue...")
                break
            iters += 1
            time.sleep(1)

        # assert data_dict["FcmEgress.num_pkt.f1"][0] == total_pkts_sends, "Error: Packets are not correctly inserted..."

        fcm_l1_d1 = self.fcm_l1_d1
        fcm_l2_d1 = self.fcm_l2_d1
        fcm_l3_d1 = self.fcm_l3_d1
        fcm_l1_d2 = self.fcm_l1_d2
        fcm_l2_d2 = self.fcm_l2_d2
        fcm_l3_d2 = self.fcm_l3_d2

        logger.info("[INFO-FCM] Start parsing in tables...")
        # Get all FCM entries
        fcm_table = [[], []]
        # fcm_table[0].append(self.evaluate_table(self.fcm_tables, "fcm_l1_d1"))
        # fcm_table[0].append(self.evaluate_table(self.fcm_tables, "fcm_l2_d1"))
        # fcm_table[0].append(self.evaluate_table(self.fcm_tables, "fcm_l3_d1"))
        # fcm_table[1].append(self.evaluate_table(self.fcm_tables, "fcm_l1_d2"))
        # fcm_table[1].append(self.evaluate_table(self.fcm_tables, "fcm_l2_d2"))
        # fcm_table[1].append(self.evaluate_table(self.fcm_tables, "fcm_l3_d2"))

        fcm_table[0].append([0] * SKETCH_W1)
        fcm_table[0].append([0] * SKETCH_W2)
        fcm_table[0].append([0] * SKETCH_W3)
        fcm_table[1].append([0] * SKETCH_W1)
        fcm_table[1].append([0] * SKETCH_W2)
        fcm_table[1].append([0] * SKETCH_W3)

        # call the register values and get flow size estimation
        logger.info("[INFO-FCM] Start query processing...")
        ARE = 0
        AAE = 0
        for key, value in in_tuples.items():
            logger.info(f"Getting data from {key}")
            ## depth 1, level 1
            # 0x162AE6FC
            val_d1 = 0
            hash_d1 = fcm_crc32(key) % SKETCH_W1
            register_l1_d1 = fcm_l1_d1
            resp_l1_d1 = register_l1_d1.entry_get(target,
                    [register_l1_d1.make_key([gc.KeyTuple('$REGISTER_INDEX', hash_d1)])],
                    {"from_hw": FROM_HW})
            data_d1, _ = next(resp_l1_d1)
            data_d1_dict = data_d1.to_dict()
            val_s1_d1 = data_d1_dict["FcmEgress.fcmsketch.sketch_reg_l1_d1.f1"][0]
            val_d1 = val_s1_d1


            fcm_table[0][0][hash_d1] = val_s1_d1
            logger.info(f"Store s1 value {val_s1_d1} in {hash_d1}")
            # overflow to level 2?
            if (val_s1_d1 == ADD_LEVEL1):
                hash_d1 = int(hash_d1 / 8)
                register_l2_d1 = fcm_l2_d1
                resp_l2_d1 = register_l2_d1.entry_get(target,
                        [register_l2_d1.make_key([gc.KeyTuple('$REGISTER_INDEX', hash_d1)])],
                        {"from_hw": FROM_HW})
                data_d1, _ = next(resp_l2_d1)
                data_d1_dict = data_d1.to_dict()
                val_s2_d1 = data_d1_dict["FcmEgress.fcmsketch.sketch_reg_l2_d1.f1"][0]
                val_d1 = val_s2_d1 + ADD_LEVEL1 - 1

                fcm_table[0][1][hash_d1] = val_s2_d1
                logger.info(f"Store s2 value {val_s2_d1} in {hash_d1}")
                # overflow to level 3?
                if (val_d1 == ADD_LEVEL2):
                    hash_d1 = int(hash_d1 / 8)
                    register_l3_d1 = fcm_l3_d1
                    resp_l3_d1 = register_l3_d1.entry_get(target,
                            [register_l3_d1.make_key([gc.KeyTuple('$REGISTER_INDEX', hash_d1)])],
                            {"from_hw": FROM_HW})
                    data_d1, _ = next(resp_l3_d1)
                    data_d1_dict = data_d1.to_dict()
                    val_s3_d1 = data_d1_dict["FcmEgress.fcmsketch.sketch_reg_l3_d1.f1"][0]
                    val_d1 = val_s3_d1 + ADD_LEVEL2 - 1

                    fcm_table[0][2][hash_d1] = val_s3_d1
                    logger.info(f"Store s3 value {val_s3_d1} in {hash_d1}")


            ## depth 2, level 1
            val_d2 = 0
            hash_d2 = fcm_crc32_init_val(key, 0xF0000000) % SKETCH_W1
            register_l1_d2 = fcm_l1_d2
            resp_l1_d2 = register_l1_d2.entry_get(target,
                    [register_l1_d2.make_key([gc.KeyTuple('$REGISTER_INDEX', hash_d2)])],
                    {"from_hw": FROM_HW})
            data_d2, _ = next(resp_l1_d2)
            data_d2_dict = data_d2.to_dict()
            val_s1_d2 = data_d2_dict["FcmEgress.fcmsketch.sketch_reg_l1_d2.f1"][0]
            val_d2 = val_s1_d2

            fcm_table[1][0][hash_d2] = val_s1_d2
            logger.info(f"Store s1 value {val_s1_d2} in {hash_d2}")

            # overflow to level 2?
            if (val_s1_d2 == ADD_LEVEL1):
                hash_d2 = int(hash_d2 / 8)
                register_l2_d2 = fcm_l2_d2
                resp_l2_d2 = register_l2_d2.entry_get(target,
                        [register_l2_d2.make_key([gc.KeyTuple('$REGISTER_INDEX', hash_d2)])],
                        {"from_hw": FROM_HW})
                data_d2, _ = next(resp_l2_d2)
                data_d2_dict = data_d2.to_dict()
                val_s2_d2 = data_d2_dict["FcmEgress.fcmsketch.sketch_reg_l2_d2.f1"][0]
                val_d2 = val_s2_d2 + ADD_LEVEL1 - 1

                fcm_table[1][1][hash_d2] = val_s2_d2
                logger.info(f"Store s2 value {val_s2_d2} in {hash_d2}")

                # overflow to level 3?
                if (val_d2 == ADD_LEVEL2):
                    hash_d2 = int(hash_d2 / 8)
                    register_l3_d2 = fcm_l3_d2
                    resp_l3_d2 = register_l3_d2.entry_get(target,
                            [register_l3_d2.make_key([gc.KeyTuple('$REGISTER_INDEX', hash_d2)])],
                            {"from_hw": FROM_HW})
                    data_d2, _ = next(resp_l3_d2)
                    data_d2_dict = data_d2.to_dict()
                    val_s3_d2 = data_d2_dict["FcmEgress.fcmsketch.sketch_reg_l3_d2.f1"][0]
                    val_d2 = val_s3_d2 + ADD_LEVEL2 - 1

                    fcm_table[1][2][hash_d2] = val_s3_d2
                    logger.info(f"Store s3 value {val_s3_d2} in {hash_d2}")


            if DEBUGGING:
                logger.info("[INFO-FCM] Flow %d - True : %d, Est of FCM : %d", key, value, min(val_s1_d1, val_s1_d2))

            final_query = min(val_d1, val_d2)
            ARE += abs(final_query - value) / float(value)
            AAE += abs(final_query - value) / 1.0
        logger.info(bcolors.OKBLUE + "[INFO-FCM] Flow Size - ARE = %2.8f" + bcolors.ENDC, (ARE / NUM_FLOWS))
        logger.info(bcolors.OKBLUE + "[INFO-FCM] Flow Size - AAE = %2.8f" + bcolors.ENDC, (AAE / NUM_FLOWS))
        logger.info("[WaterfallFcm] Start EM FSD...")

        s1 = [fcm_table[0][0], fcm_table[1][0]]
        s2 = [fcm_table[0][1], fcm_table[1][1]]
        s3 = [fcm_table[0][2], fcm_table[1][2]]
        em_fsd = EM_WFCM(s1, s2, s3, tuples.values())
        ns = em_fsd.run_em(5)
        # logger.info(fsd)
        logger.info(f"{ns[-1]} - sz {len(ns)}, {fsd[-1]} - sz {len(fsd)}")

        wmre_nom = 0.0
        wmre_denom = 0.0
        for real, est in zip(fsd, ns):
            wmre_nom += abs(float(real) - est)
            wmre_denom += (float(real) + est) / 2

        wmre = wmre_nom / wmre_denom

        
        logger.info(bcolors.OKBLUE + f"[WaterfallFcm] WMRE : {wmre : .2f}" + bcolors.ENDC)
        logger.info(f"[WaterfallFcm] Finished EM FSD")
