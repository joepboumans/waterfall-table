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
import numpy as np

swports = get_sw_ports()
project_name = 'waterfall'
logger = logging.getLogger(project_name)

if not len(logger.handlers):
    sh = logging.StreamHandler()
    formatter = logging.Formatter('[%(levelname)s - %(name)s - %(funcName)s]: %(message)s')
    sh.setFormatter(formatter)
    sh.setLevel(logging.INFO)
    logger.addHandler(sh)

class DigestResubmitTest(BfRuntimeTest):
    # Input a number of entries and verify if the resub and digest is working
    def setUp(self):
        logger.info("Starting setup")
        client_id = 0
        BfRuntimeTest.setUp(self, client_id)
        logger.info("\tfinished BfRuntimeSetup")
        self.bfrt_info = self.interface.bfrt_info_get(project_name)

        # Get resubmission and port metadata tables
        self.port_meta = self.bfrt_info.table_get("$PORT_METADATA")
        self.resub = self.bfrt_info.table_get("resub")

        # Get digest/learn filter from gRPC
        self.learn_filter = self.bfrt_info.learn_get("digest")
        self.learn_filter.info.data_field_annotation_add("src_addr", "ipv4")
        self.learn_filter.info.data_field_annotation_add("dst_addr", "ipv4")

        # Get Waterfall tables
        self.table_1 = self.bfrt_info.table_get("table_1")
        self.table_2 = self.bfrt_info.table_get("table_2")
        self.table_3 = self.bfrt_info.table_get("table_3")
        self.table_4 = self.bfrt_info.table_get("table_4")
        self.table_dict = {"table_1" : self.table_1, "table_2" : self.table_2, "table_3" : self.table_3, "table_4" : self.table_4}

        self.target = gc.Target(device_id=0, pipe_id=0xffff)
        logger.info("Finished setup")

    def runTest(self):
        logger.info("Start testing")
        ig_port = swports[0]
        target = self.target
        resub = self.resub

        num_entries = 10
        seed = 1001
        ip_list = self.generate_random_ip_list(num_entries, seed)
        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        key = resub.make_key([gc.KeyTuple('ig_md.found', True)])
        data = resub.make_data([], "SwitchIngress.no_resub")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_md.found', False)])
        data = resub.make_data([], "SwitchIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        for ip_entry in ip_list:
            src_addr = getattr(ip_entry, "ip")
            ig_port = swports[2]

            ''' TC:2 Send, receive and verify packets'''
            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr)
            logger.info("Sending simple packet to switch")
            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, ig_port)
            logger.info("..packet received correctly")

        ''' TC:3 Get data from the digest'''
        self.evalutate_digest(num_entries)

        ''' TC:4 Validate received digest data'''
        logger.info("Only Table 1 should have been filled")
        self.evalutate_table("table_1")
        self.evalutate_table("table_2")
        self.evalutate_table("table_3")
        self.evalutate_table("table_4")

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
                recv_dst_addr = data_dict["dst_addr"]
                recv_src_port = data_dict["src_port"]
                recv_dst_port = data_dict["dst_port"]
                recv_protocol = data_dict["protocol"]
                recv_remain1 = data_dict["remain1"]
                recv_remain2 = data_dict["remain2"]
                recv_remain3 = data_dict["remain3"]
                recv_remain4 = data_dict["remain4"]
                logger.info(f"{recv_src_addr = } : {recv_dst_addr = } | {recv_src_port = } {recv_dst_port = } | {recv_protocol = } | {recv_remain1} {recv_remain2} {recv_remain3} {recv_remain4}")
            try:
                digest = self.interface.digest_get()
            except:
                break;
        assert(total_recv == num_entries)

    def evalutate_table(self, name):
        table = self.table_dict[name]
        summed = 0
        nonzero_entries = 0
        data_table = table.entry_get(self.target, [], {"from_hw" : True})
        for data, key in data_table:
            data_dict = data.to_dict()
            entry_val = data_dict[f"SwitchIngress.{name}.f1"][0]
            if entry_val != 0:
                summed += entry_val
                nonzero_entries += 1
                # logger.info(data_dict)
                # logger.info(entry_val.to_bytes(2,'big'))

        logger.info(f"{name} has {summed} total remainders and {nonzero_entries} entries")

    def tearDown(self):
        logger.info("Tearing down test")
        self.resub.entry_del(self.target)
        self.port_meta.entry_del(self.target)

        self.table_1.entry_del(self.target)
        self.table_2.entry_del(self.target)
        self.table_3.entry_del(self.target)
        self.table_4.entry_del(self.target)

        BfRuntimeTest.tearDown(self)

class PassAllTables(BfRuntimeTest):
    # Always resubmit and thereby bypass lookup. By this we can verify that all tables can be accessed
    def setUp(self):
        logger.info("Starting setup")
        client_id = 0
        BfRuntimeTest.setUp(self, client_id)
        logger.info("\tfinished BfRuntimeSetup")
        self.bfrt_info = self.interface.bfrt_info_get(project_name)

        # Get resubmission and port metadata tables
        self.port_meta = self.bfrt_info.table_get("$PORT_METADATA")
        self.resub = self.bfrt_info.table_get("resub")

        # Get digest/learn filter from gRPC
        self.learn_filter = self.bfrt_info.learn_get("digest")
        self.learn_filter.info.data_field_annotation_add("src_addr", "ipv4")
        self.learn_filter.info.data_field_annotation_add("dst_addr", "ipv4")

        # Get Waterfall tables
        self.table_1 = self.bfrt_info.table_get("table_1")
        self.table_2 = self.bfrt_info.table_get("table_2")
        self.table_3 = self.bfrt_info.table_get("table_3")
        self.table_4 = self.bfrt_info.table_get("table_4")
        self.table_dict = {"table_1" : self.table_1, "table_2" : self.table_2, "table_3" : self.table_3, "table_4" : self.table_4}

        self.target = gc.Target(device_id=0, pipe_id=0xffff)
        logger.info("Finished setup")

    def runTest(self):
        logger.info("Start testing")
        ig_port = swports[0]
        target = self.target
        resub = self.resub

        learn_filter = self.learn_filter

        num_entries = 5
        seed = 1001
        ip_list = self.generate_random_ip_list(num_entries, seed)
        ''' TC:1 Setting up port_metadata and resub'''
        logger.info("Populating resub table...")
        logger.debug(f"\tresub - inserting table entry with port {ig_port}")

        # Resubmit even when the packet was found in tables
        key = resub.make_key([gc.KeyTuple('ig_md.found', True)])
        data = resub.make_data([], "SwitchIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        key = resub.make_key([gc.KeyTuple('ig_md.found', False)])
        data = resub.make_data([], "SwitchIngress.resubmit_hdr")
        resub.entry_add(target, [key], [data])

        for ip_entry in ip_list:
            src_addr = getattr(ip_entry, "ip")
            ig_port = swports[2]

            ''' TC:2 Send, receive and verify packets'''
            pkt_in = testutils.simple_tcp_packet(ip_src=src_addr)
            logger.info("Sending 5 idenitcal packets to flow through all the tables")
            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, ig_port)

            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, ig_port)

            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, ig_port)

            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, ig_port)

            testutils.send_packet(self, ig_port, pkt_in)
            testutils.verify_packet(self, pkt_in, ig_port)
            logger.info("..all packets sent and received")

        ''' TC:3 Get data from the digest'''
        self.evalutate_digest(num_entries * 5)

        ''' TC:4 Validate received digest data'''
        logger.info(f"All tables should have {num_entries} of entries")
        self.evalutate_table("table_1")
        self.evalutate_table("table_2")
        self.evalutate_table("table_3")
        self.evalutate_table("table_4")

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
                recv_dst_addr = data_dict["dst_addr"]
                recv_src_port = data_dict["src_port"]
                recv_dst_port = data_dict["dst_port"]
                recv_protocol = data_dict["protocol"]
                recv_remain1 = data_dict["remain1"]
                recv_remain2 = data_dict["remain2"]
                recv_remain3 = data_dict["remain3"]
                recv_remain4 = data_dict["remain4"]
                logger.info(f"{recv_src_addr = } : {recv_dst_addr = } | {recv_src_port = } {recv_dst_port = } | {recv_protocol = } | {recv_remain1} {recv_remain2} {recv_remain3} {recv_remain4}")
            try:
                digest = self.interface.digest_get()
            except:
                break;
        assert(total_recv == num_entries)

    def evalutate_table(self, name):
        table = self.table_dict[name]
        summed = 0
        nonzero_entries = 0
        data_table = table.entry_get(self.target, [], {"from_hw" : True})
        for data, key in data_table:
            data_dict = data.to_dict()
            entry_val = data_dict[f"SwitchIngress.{name}.f1"][0]
            if entry_val != 0:
                summed += entry_val
                nonzero_entries += 1

        logger.info(f"{name} has {summed} total remainders and {nonzero_entries} entries")

    def tearDown(self):
        logger.info("Tearing down test")
        self.resub.entry_del(self.target)
        self.port_meta.entry_del(self.target)

        self.table_1.entry_del(self.target)
        self.table_2.entry_del(self.target)
        self.table_3.entry_del(self.target)
        self.table_4.entry_del(self.target)

        BfRuntimeTest.tearDown(self)
