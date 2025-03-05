#!/usr/bin/python3
from enum import unique
import logging
from collections import namedtuple, defaultdict
from math import radians
import random
import re
import mmap

from ptf import config
import ptf.testutils as testutils
from p4testutils.misc_utils import *
from bfruntime_client_base_tests import BfRuntimeTest
import bfrt_grpc.bfruntime_pb2 as bfruntime_pb2
import bfrt_grpc.client as gc
from utils import *
from fcm_utils import *

WATERFALL_WIDTH = 65536 # 2 ^ IDX_BIT_WIDTH - 1 = WATERFALL_WIDTH

swports = get_sw_ports()
project_name = 'waterfall_fcm'
logger = logging.getLogger(project_name)

if not len(logger.handlers):
    sh = logging.StreamHandler()
    formatter = logging.Formatter('[%(levelname)s - %(name)s - %(funcName)s]: %(message)s')
    sh.setFormatter(formatter)
    sh.setLevel(logging.INFO)
    logger.addHandler(sh)

class WaterfallUnitTests(BfRuntimeTest):
    def setUp(self):
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

        self.target = gc.Target(device_id=0, pipe_id=0xffff)
        logger.info("...tables setup done!")

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

    def clearWaterfall(self):
        logger.info("Resetting switch..")
        self.forward.entry_del(self.target)
        self.resub.entry_del(self.target)
        # self.parse_resub.entry_del(self.target)
        self.port_meta.entry_del(self.target)

        for _, table in self.table_dict.items():
            table.entry_del(self.target)

        for _, swap in self.swap_dict.items():
            swap.entry_del(self.target)
        logger.info("...cleared all tables")

    def resetWaterfall(self):
        self.clearWaterfall()
        self.setupWaterfall()

    def tearDown(self):
        logger.info("Tearing down test")
        self.forward.entry_del(self.target)
        self.resub.entry_del(self.target)
        self.port_meta.entry_del(self.target)

        for _, table in self.table_dict.items():
            table.entry_del(self.target)

        for _, swap in self.swap_dict.items():
            swap.entry_del(self.target)

        BfRuntimeTest.tearDown(self)

    def runTest(self):
        logger.info("Start testing")

        self.testDigest()
        self.resetWaterfall()
        self.testPassAllTables()
        self.resetWaterfall()
        self.testLargeInserts()

    def evalutate_digest(self, num_entries):
        learn_filter = self.learn_filter
        total_recv = 0
        digest = self.interface.digest_get()
        recv_tuples = []
        while(digest != None):
            data_list = learn_filter.make_data_list(digest)
            logger.info(f"Received {len(data_list)} entries from the digest")
            total_recv += len(data_list)
            for data in data_list:
                data_dict = data.to_dict()
                recv_src_addr = data_dict["src_addr"]
                logger.info(f"{recv_src_addr = }")
                bytes_src = bytes( data["src_addr"].val)
                # logger.info(f"{bytes_src.hex()}")
                recv_tuples.append(bytes_src)
            try:
                digest = self.interface.digest_get()
            except:
                break;
        print(f"{total_recv} == {num_entries}")
        assert(total_recv == num_entries)
        return recv_tuples

    def evalutate_table(self, name):
        table = self.table_dict[name]
        summed = 0
        nonzero_entries = 0
        data_table = table.entry_get(self.target, [], {"from_hw" : True})
        for data, key in data_table:
            data_dict = data.to_dict()
            entry_val = data_dict[f"WaterfallIngress.{name}.f1"][0]
            if entry_val != 0:
                summed += entry_val
                nonzero_entries += 1
                # logger.info(data_dict)
                # logger.info(entry_val.to_bytes(2,'big'))

        logger.info(f"{name} has {summed} total remainders and {nonzero_entries} entries")

    def evalutateEntryInTable(self, name, flowId):
        # Get the correct inital value for the crc32
        # Is reveresed to the number in P4 code
        num = int(re.search(r'\d+', name).group())
        init_val = 0x0
        match num:
            case 1: 
                init_val = 0xFFFFFFFF
            case 2:
                init_val = 0x0FFFFFFF
            case 3:
                init_val = 0x00FFFFFF
            case 4:
                init_val = 0x000FFFFF

        table = self.table_dict[name]
        idx = crc32_sf(flowId, init_val) % WATERFALL_WIDTH 
        # logger.info(f"idx of {flowId.hex()} : {idx}")
        key = table.make_key([gc.KeyTuple('$REGISTER_INDEX', idx)])
        resp_table = table.entry_get(self.target, [key], {"from_hw" : True})
        data, _ = next(resp_table)
        data_dict = data.to_dict()
        entry_val = data_dict[f"WaterfallIngress.{name}.f1"][0]
        if entry_val > 0:
            logger.info(f"{name} : {entry_val.to_bytes(2,'big').hex()}")

    def testDigest(self):
        ig_port = swports[0]
        eg_port = swports[1]

        target = self.target
        forward = self.forward
        resub = self.resub

        key = forward.make_key([gc.KeyTuple('ig_intr_md.ingress_port', ig_port)])
        data = forward.make_data([gc.DataTuple('dst_port', eg_port)], "WaterfallIngress.hit")
        forward.entry_add(target, [key], [data])

        num_entries = 10
        seed = 1001
        ip_list = self.generate_random_ip_list(num_entries, seed)
        raw_ip_list = []
        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        # Only resubmit if both are found
        for i in range(1, 5):
            key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', False), gc.KeyTuple('ig_md.found_hi', i), gc.KeyTuple('ig_md.found_lo', i)])
            data = resub.make_data([], "WaterfallIngress.no_action")
            resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', True)])
        data = resub.make_data([], "WaterfallIngress.no_action")
        resub.entry_add(target, [key], [data])

        for name, table in self.swap_dict.items():
            self.addSwapEntry(table, name)

        MAX_FLOW_SIZE = 100          # max size of flows
        for ip_entry in ip_list:
            src_addr = getattr(ip_entry, "ip")

            ''' TC:2 Send, receive and verify packets'''
            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr)
            logger.info("Sending simple packet to switch")
            flow_size = int(min(MAX_FLOW_SIZE, max(MAX_FLOW_SIZE * abs(random.gauss(mu=0, sigma=0.0001)), 1.0)))
            testutils.send_packet(self, ig_port, pkt_in, count=flow_size)
            testutils.verify_packet(self, pkt_in, eg_port)
            logger.info("..packet received correctly")

            raw_src_addr = bytes([int(x) for x in src_addr.split('.')])
            raw_ip_list.append(raw_src_addr)
            logger.info(ip_entry)

        ''' TC:3 Get data from the digest'''
        self.evalutate_digest(num_entries)

        ''' TC:4 Validate received digest data'''
        logger.info("Only Table 1 should have been filled")
        for src_addr in raw_ip_list:
            logger.info(src_addr.hex())
            for key, data in self.table_dict.items():
                self.evalutateEntryInTable(key, src_addr)


    
    def testPassAllTables(self):
        ig_port = swports[0]
        eg_port = swports[1]
        target = self.target
        forward = self.forward
        resub = self.resub

        key = forward.make_key([gc.KeyTuple('ig_intr_md.ingress_port', ig_port)])
        data = forward.make_data([gc.DataTuple('dst_port', eg_port)], "WaterfallIngress.hit")
        forward.entry_add(target, [key], [data])

        learn_filter = self.learn_filter
        
        num_entries = 1
        seed = 1001
        ip_list = self.generate_random_ip_list(num_entries, seed)
        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        # Resubmit even when the packet was found in tables
        key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0)])
        data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x1)])
        data = resub.make_data([], "WaterfallIngress.no_action")
        resub.entry_add(target, [key], [data])

        for name, table in self.swap_dict.items():
            self.addSwapEntry(table, name)

        raw_ip_list = []
        for ip_entry in ip_list:
            src_addr = getattr(ip_entry, "ip")

            ''' TC:2 Send, receive and verify packets'''
            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr)
            logger.info("Sending 4 identical packets to flow through all the tables")
            for _ in range(5):
                logger.info("Sending...")
                testutils.send_packet(self, ig_port, pkt_in)
                testutils.verify_packet(self, pkt_in, eg_port)
                logger.info("...verified received")

            raw_src_addr = bytes([int(x) for x in src_addr.split('.')])
            raw_ip_list.append(raw_src_addr)
            logger.info("..all packets sent and received")

        ''' TC:3 Get data from the digest'''
        self.evalutate_digest(num_entries)

        ''' TC:4 Validate received digest data'''
        logger.info(f"All tables should have {num_entries} entries")

        # Should show all tables having stored packets
        for src_addr in raw_ip_list:
            logger.info(src_addr.hex())
            for key, data in self.table_dict.items():
                self.evalutateEntryInTable(key, src_addr)

        # for key, data in self.table_dict.items():
        #     self.evalutate_table(key)

    def read_data_set(self, data_name):
        print(f"[Dataset Loader] Get data from {data_name}")
        first = True

        tuples = defaultdict(int)
        with open(data_name, "r+b") as of:
            with mmap.mmap(of.fileno(), length=0, access=mmap.ACCESS_READ) as f:
                data = f.read()
            print(f"[Dataset Loader] Loaded in dataset into memory")
            for i in range(0, len(data), 13):
                if i + 12 >= len(data):
                    break

                # Read src addr
                tuples[data[i + 0:i + 4]] += 1
                    
                if first:
                    print(*tuples.keys())
                    first = False

        max_count = max(tuples.values())
        fsd = [0] * (max_count + 1)
        print(f"[WaterfallFcm - verify] Setup real EM with {max_count = }...")
        for val in tuples.values():
            fsd[val] += 1

        count = 0
        for i, fs in zip(range(max_count + 1), fsd):
            if fs != 0:
                print(f"{i} : {fs}", end=" ")
                count += 1
                if count % 10 == 0:
                    print("")
        print("")

        # delay = 10
        # print(f"[Dataset Loader] ...done! Waiting for {delay}s before starting test...")
        # time.sleep(delay)
        print(f"[Dataset Loader] Parse data into tuples, found {len(tuples)} tuples!")

        # for t in tuples:
        #     print(t.hex())

        print("[Dataset Loader] Done!")
        return tuples

    def testLargeInserts(self):
        ig_port = swports[0]
        eg_port = swports[1]
        target = self.target
        forward = self.forward
        resub = self.resub

        key = forward.make_key([gc.KeyTuple('ig_intr_md.ingress_port', ig_port)])
        data = forward.make_data([gc.DataTuple('dst_port', eg_port)], "WaterfallIngress.hit")
        forward.entry_add(target, [key], [data])

        in_tuples = self.read_data_set("/workspace/PDS-Simulator/data/1024_test.dat")
        src_ip_list = []
        for tup in in_tuples:
            src_ip_list.append(".".join([str(x) for x in tup]))
        # src_ip_list = self.generate_random_ip_list(num_entries_src, seed)

        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        # Only resubmit if both are found
        for i in range(1, 5):
            key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', False), gc.KeyTuple('ig_md.found_hi', i), gc.KeyTuple('ig_md.found_lo', i)])
            data = resub.make_data([], "WaterfallIngress.no_action")
            resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', True)])
        data = resub.make_data([], "WaterfallIngress.no_action")
        resub.entry_add(target, [key], [data])

        for name, table in self.swap_dict.items():
            self.addSwapEntry(table, name)

        logger.info(f"Start sending {len(src_ip_list)} entries")
        raw_ip_list = []
        for src_addr in src_ip_list:
            src_port = random.randrange(0, 0xFFFF)
            dst_port = random.randrange(0, 0xFFFF)

            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr, tcp_sport=src_port, tcp_dport=dst_port)
            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, eg_port)

            raw_src_addr = bytes([int(x) for x in src_addr.split('.')])
            raw_ip_list.append(raw_src_addr)
        logger.info(f"...done sending")

        ''' TC:3 Look for data in digest'''
        unique_tx_tuples = set(in_tuples)
        recv_tuples = self.evalutate_digest(len(unique_tx_tuples) )
        unique_recv_tuples = set(recv_tuples)
        logger.info(f"Found total of {len(recv_tuples)} tuples with {len(unique_recv_tuples)} unique tuples")
        assert(len(in_tuples) == len(unique_recv_tuples))

        ''' TC:4 Validate received digest data'''
        for recv_src_addr in unique_recv_tuples:
            logger.info(recv_src_addr.hex())
            for key, data in self.table_dict.items():
                self.evalutateEntryInTable(key, recv_src_addr)

        # for src_addr in raw_ip_list:
            # logger.info(src_addr.hex())
            # for key, data in self.table_dict.items():
                # self.evalutateEntryInTable(key, src_addr)
