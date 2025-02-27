#!/usr/bin/python3
import logging
from collections import namedtuple
from math import radians
import random

from ptf import config
import ptf.testutils as testutils
from p4testutils.misc_utils import *
from bfruntime_client_base_tests import BfRuntimeTest
import bfrt_grpc.bfruntime_pb2 as bfruntime_pb2
import bfrt_grpc.client as gc
from utils import *

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
        # self.parse_resub = self.bfrt_info.table_get("parse_resub_hdr")

        # Get digest/learn filter from gRPC
        self.learn_filter = self.bfrt_info.learn_get("digest")
        self.learn_filter.info.data_field_annotation_add("src_addr", "ipv4")

        # Get Waterfall tables
        self.table_dict = {}
        table_names = ["table_1", "table_2"]
        for sname in table_names:
            for loc in ["hi", "lo"]:
                name = f"{sname}_{loc}"
                table = self.bfrt_info.table_get(name)
                self.table_dict.update({name : table})



        # Get swap tables
        self.swap_dict = {}
        swap_names = ["swap1", "swap2"]
        for sname in swap_names:
            for loc in ["hi", "lo"]:
                name = f"{sname}_{loc}"
                swap = self.bfrt_info.table_get(name)
                self.swap_dict.update({name : swap})

        self.target = gc.Target(device_id=0, pipe_id=0xffff)
        logger.info("...tables setup done!")

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

        # self.testDigest()
        # self.resetWaterfall()
        self.testPassAllTables()
        self.resetWaterfall()
        # self.testLargeInserts()

    def evalutate_digest(self, num_entries):
        learn_filter = self.learn_filter
        total_recv = 0
        digest = self.interface.digest_get()
        while(digest != None):
            data_list = learn_filter.make_data_list(digest)
            logger.info(f"Received {len(data_list)} entries from the digest")
            total_recv += len(data_list)
            for data in data_list:
                data_dict = data.to_dict()
                recv_src_addr = data_dict["src_addr"]
                logger.info(f"{recv_src_addr = }")
            try:
                digest = self.interface.digest_get()
            except:
                break;
        print(f"{total_recv} == {num_entries}")
        assert(total_recv == num_entries)

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
                logger.info(data_dict)
                # logger.info(entry_val.to_bytes(2,'big'))

        logger.info(f"{name} has {summed} total remainders and {nonzero_entries} entries")

    def testDigest(self):
        ig_port = swports[0]
        eg_port = swports[1]

        target = self.target
        forward = self.forward
        resub = self.resub
        parse_resub = self.parse_resub

        key = forward.make_key([gc.KeyTuple('ig_intr_md.ingress_port', ig_port)])
        data = forward.make_data([gc.DataTuple('dst_port', eg_port)], "WaterfallIngress.hit")
        forward.entry_add(target, [key], [data])

        swap1 = self.swap1
        swap2 = self.swap2
        swap3 = self.swap3
        swap4 = self.swap4

        num_entries = 1
        seed = 1001
        ip_list = self.generate_random_ip_list(num_entries, seed)
        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        key = resub.make_key([gc.KeyTuple('ig_md.found', True)])
        data = resub.make_data([], "WaterfallIngress.no_action")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_md.found', False), gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0)])
        data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        key = parse_resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0)])
        data = parse_resub.make_data([], "WaterfallIngress.no_action")
        parse_resub.entry_add(target, [key], [data])

        key = parse_resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x1)])
        data = parse_resub.make_data([], "WaterfallIngress.parse_hdr")
        parse_resub.entry_add(target, [key], [data])

        for name, table in self.swap_dict.items():
            self.addSwapEntry(table, name)

        for ip_entry in ip_list:
            src_addr = getattr(ip_entry, "ip")

            ''' TC:2 Send, receive and verify packets'''
            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr)
            logger.info("Sending simple packet to switch")
            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, eg_port)
            logger.info("..packet received correctly")

        ''' TC:3 Get data from the digest'''
        self.evalutate_digest(num_entries)

        ''' TC:4 Validate received digest data'''
        logger.info("Only Table 1 should have been filled")
        for key, data in self.table_dict.items():
            self.evalutate_table(key)


    def addSwapEntry(self, table, name):
        num = name.replace("swap", "")
        logger.info(num)
        key = table.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0), gc.KeyTuple('ig_md.found_hi', False), gc.KeyTuple('ig_md.found_lo', False)])
        data = table.make_data([], f"WaterfallIngress.lookup{num}")
        table.entry_add(self.target, [key], [data])

        key = table.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x1)])
        data = table.make_data([], f"WaterfallIngress.do_swap{num}")
        table.entry_add(self.target, [key], [data])

    
    def testPassAllTables(self):
        ig_port = swports[0]
        eg_port = swports[1]
        # ig_port = 132 # hwports can be 132, 140, 148, 156
        # eg_port = hwports[ig_port]
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
        key = resub.make_key([gc.KeyTuple('ig_md.found_hi', True), gc.KeyTuple('ig_md.found_lo', True)])
        data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_md.found_hi', False), gc.KeyTuple('ig_md.found_lo', False)])
        data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_md.found_hi', True), gc.KeyTuple('ig_md.found_lo', False)])
        data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_md.found_hi', False), gc.KeyTuple('ig_md.found_lo', True)])
        data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        # parse_resub = self.parse_resub
        # key = parse_resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x0)])
        # data = parse_resub.make_data([], "WaterfallIngress.no_action")
        # parse_resub.entry_add(target, [key], [data])
        #
        # key = parse_resub.make_key([gc.KeyTuple('ig_intr_md.resubmit_flag', 0x1)])
        # data = parse_resub.make_data([], "WaterfallIngress.parse_hdr")
        # parse_resub.entry_add(target, [key], [data])

        for name, table in self.swap_dict.items():
            self.addSwapEntry(table, name)

        for ip_entry in ip_list:
            src_addr = getattr(ip_entry, "ip")

            ''' TC:2 Send, receive and verify packets'''
            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr)
            logger.info("Sending 5 idenitcal packets to flow through all the tables")
            for _ in range(3):
                testutils.send_packet(self, ig_port, pkt_in)
                testutils.verify_packet(self, pkt_in, eg_port)

            logger.info("..all packets sent and received")

        ''' TC:3 Get data from the digest'''
        self.evalutate_digest(num_entries)

        ''' TC:4 Validate received digest data'''
        logger.info(f"All tables should have {num_entries} entries")
        for key, data in self.table_dict.items():
            self.evalutate_table(key)

    def testLargeInserts(self):
        ig_port = swports[0]
        eg_port = swports[1]
        # ig_port = 132 # hwports can be 132, 140, 148, 156
        # eg_port = hwports[ig_port]
        target = self.target
        forward = self.forward
        resub = self.resub

        key = forward.make_key([gc.KeyTuple('ig_intr_md.ingress_port', ig_port)])
        data = forward.make_data([gc.DataTuple('dst_port', eg_port)], "WaterfallIngress.hit")
        forward.entry_add(target, [key], [data])

        num_entries_src = 1000
        num_entries_dst = 100
        seed = 1001
        random.seed(seed)
        src_ip_list = self.generate_random_ip_list(num_entries_src, seed)
        dst_ip_list = self.generate_random_ip_list(num_entries_dst, seed + 1)

        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        swap1 = self.swap1
        swap2 = self.swap2
        swap3 = self.swap3
        swap4 = self.swap4

        key = resub.make_key([gc.KeyTuple('ig_md.found', True)])
        data = resub.make_data([], "WaterfallIngress.no_action")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_md.found', False)])
        data = resub.make_data([], "WaterfallIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        for name, table in self.swap_dict.items():
            self.addSwapEntry(table, name)

        logger.info(f"Start sending {num_entries_src * num_entries_dst} entries")
        for src_ip in src_ip_list:
            for dst_ip in dst_ip_list:
                src_addr = getattr(src_ip, "ip")
                dst_addr = getattr(dst_ip, "ip")
                src_port = random.randrange(0, 0xFFFF)
                dst_port = random.randrange(0, 0xFFFF)

                pkt_in = testutils.simple_tcp_packet(ip_src=src_addr, ip_dst=dst_addr, tcp_sport=src_port, tcp_dport=dst_port)
                testutils.send_packet(self, ig_port, pkt_in)
                testutils.verify_packet(self, pkt_in, eg_port)
        logger.info(f"...done sending")

        ''' TC:3 Look for data in digest'''
        self.evalutate_digest(num_entries_src * num_entries_dst)

        ''' TC:4 Validate received digest data'''
        for key, data in self.table_dict.items():
            self.evalutate_table(key)
