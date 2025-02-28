#! /usr/bin/env python3
import os, sys, subprocess
import re
import mmap
import time
import utils
import argparse
import logging

from collections import defaultdict

from EM_ctypes import EM_WFCM
from utils import *

sys.path.append('/home/onie/sde/bf-sde-9.11.0/install/lib/python3.8/site-packages/tofino/')
sys.path.append('/home/onie/sde/bf-sde-9.11.0/install/lib/python3.8/site-packages/tofino/bfrt_grpc/')

# os.environ["GRPC_ENABLE_FORK_SUPPORT"] = "1"
# os.environ["GRPC_POLL_STRATEGY"] = "poll"
# os.environ["GRPC_VERBOSITY"] = "debug"


NUM_STAGES = 3
DEPTH = 2
K = 8
W1 = 524288        
W2 = 65536        
W3 = 8192        
ADD_LEVEL1 = 255
ADD_LEVEL2 = 65789 
OVERFLOW_LEVEL1 = 255   
OVERFLOW_LEVEL2 = 65535 
WATERFALL_WIDTH = 65536 # 2 ^ IDX_BIT_WIDTH - 1 = WATERFALL_WIDTH

import bfrt_grpc.client as gc

project_name = 'waterfall_fcm'
logger = logging.getLogger(project_name)

if not len(logger.handlers):
    sh = logging.StreamHandler()
    formatter = logging.Formatter('[%(levelname)s - %(name)s - %(funcName)s]: %(message)s')
    sh.setFormatter(formatter)
    sh.setLevel(logging.INFO)
    logger.addHandler(sh)



class BfRt_interface():
    def __init__(self, dev, grpc_addr, client_id):
        self.isRunning = False
        self.hasFirstData = False
        self.missedDigest = 0
        self.recievedDigest = 0
        self.total_received = 0
        self.recieved_digests = []
        self.tuples = None

        self.dev_tgt = gc.Target(dev, pipe_id=0xFFFF)
        self.bfrt_info = None

        self.interface = gc.ClientInterface(grpc_addr, client_id=client_id,
                    device_id=dev, notifications=None)
        self.bfrt_info = self.interface.bfrt_info_get()
        self.p4_name = self.bfrt_info.p4_name_get()
        self.interface.bind_pipeline_config(self.p4_name)

        self.learn_filter = self.bfrt_info.learn_get("digest")
        self.learn_filter.info.data_field_annotation_add("src_addr", "ipv4")

        # Get Pkt count register of FCM
        self.num_pkt = self.bfrt_info.table_get("num_pkt")

        self.setupTables()
        self.clearTables()
        self.setupTables()

        print("Connected to Device: {}, Program: {}, ClientId: {}".format(
                dev, self.p4_name, client_id))

    def setupTables(self):
        # Get Waterfall tables
        self.table_dict = {}
        table_names = ["table_1", "table_2", "table_3", "table_4"]
        for sname in table_names:
            tables = []
            for loc in ["hi", "lo"]:
                tables.append(self.bfrt_info.table_get(f"{sname}_{loc}"))
            self.table_dict.update({sname : tables})

        # Get swap tables
        self.swap_dict = {}
        swap_names = ["swap1", "swap2", "swap3", "swap4"]
        for sname in swap_names:
            tables = []
            for loc in ["hi", "lo"]:
                tables.append(self.bfrt_info.table_get(f"{sname}_{loc}"))
            self.swap_dict.update({sname : tables})
        # Get FCM counters
        self.fcm_l1_d1 = self.bfrt_info.table_get("sketch_reg_l1_d1")
        self.fcm_l2_d1 = self.bfrt_info.table_get("sketch_reg_l2_d1")
        self.fcm_l3_d1 = self.bfrt_info.table_get("sketch_reg_l3_d1")
        self.fcm_l1_d2 = self.bfrt_info.table_get("sketch_reg_l1_d2")
        self.fcm_l2_d2 = self.bfrt_info.table_get("sketch_reg_l2_d2")
        self.fcm_l3_d2 = self.bfrt_info.table_get("sketch_reg_l3_d2")
        self.fcm_tables = {"fcm_l1_d1" : self.fcm_l1_d1, "fcm_l2_d1" : self.fcm_l2_d1, "fcm_l3_d1" : self.fcm_l3_d1, "fcm_l1_d2" : self.fcm_l1_d2, "fcm_l2_d2" : self.fcm_l2_d2, "fcm_l3_d2" : self.fcm_l3_d2}

    def clearTables(self):
        for _, tables in self.swap_dict.items():
            for table in tables:
                table.entry_del(self.dev_tgt)

        for _, tables in self.table_dict.items():
            for table in tables:
                table.entry_del(self.dev_tgt)

        for table in self.fcm_tables.values():
            table.entry_del(self.dev_tgt)



    def list_tables(self):
            for key in sorted(self.bfrt_info.table_dict.keys()):
                print(key)

    def print_table_info(self, table_name):
            print("====Table Info===")
            t = self.bfrt_info.table_get(table_name)
            print("{:<30}: {}".format("TableName", t.info.name_get()))
            print("{:<30}: {}".format("Size", t.info.size_get()))
            print("{:<30}: {}".format("Actions", t.info.action_name_list_get()))
            print("{:<30}:".format("KeyFields"))
            for field in sorted(t.info.key_field_name_list_get()):
                print("  {:<28}: {} => {}".format(field, t.info.key_field_type_get(field), t.info.key_field_match_type_get(field)))
            print("{:<30}:".format("DataFields"))
            for field in t.info.data_field_name_list_get():
                print("  {:<28}: {} {}".format(
                    "{} ({})".format(field, t.info.data_field_id_get(field)), 
                    # type(t.info.data_field_allowed_choices_get(field)), 
                    t.info.data_field_type_get(field),
                    t.info.data_field_size_get(field),
                    ))
            print("================")

    def evalutateEntryInTable(self, table, name, flowId):
        # Get the correct inital value for the crc32
        # Is reveresed to the number in P4 code
        num = int(re.search(r'\d+', name).group())
        init_val = 0x0
        if num == 1:
            init_val = 0xFFFFFFFF
        elif num == 2:
            init_val = 0x0FFFFFFF
        elif num == 3:
            init_val = 0x00FFFFFF
        elif num == 4:
            init_val = 0x000FFFFF

        idx = crc32_sf(flowId, init_val) % WATERFALL_WIDTH 
        # logger.info(f"idx of {flowId.hex()} : {idx}")
        key = table.make_key([gc.KeyTuple('$REGISTER_INDEX', idx)])
        resp_table = table.entry_get(self.dev_tgt, [key], {"from_hw" : True})
        data, _ = next(resp_table)
        data_dict = data.to_dict()
        entry_val = data_dict[f"WaterfallIngress.{name}.f1"][0]
        if entry_val > 0:
            logger.info(f"{name} : {entry_val.to_bytes(2,'big').hex()}")

    def evaluateTableFromDict(self, tables, name):
        table = tables[name]
        control_name = ""
        if "fcm" in name:
            control_name = "FcmEgress.fcmsketch"
            name = name.replace("fcm", "sketch_reg")
        else:
            control_name = "WaterfallIngress"

        summed = 0
        nonzero_entries = 0
        data_table = table.entry_get(self.dev_tgt, [])
        entries = []
        for data, key in data_table:
            data_dict = data.to_dict()
            entry_val = sum(data_dict[f"{control_name}.{name}.f1"])
            entries.append(entry_val)
            if entry_val != 0:
                summed += entry_val
                nonzero_entries += 1
                # print(data_dict)
                # print(f"{(len(entries) - 1).to_bytes(2, 'big').hex() + entry_val.to_bytes(2,'big').hex()}")
        print(f"{name} has {nonzero_entries} entries")

        return entries
    
    def evaluateTable(self, table, name):
        summed = 0
        nonzero_entries = 0
        data_table = table.entry_get(self.dev_tgt, [], {"from_hw" : True})
        for data, key in data_table:
            data_dict = data.to_dict()
            entry_val = data_dict[f"WaterfallIngress.{name}.f1"][0]
            if entry_val != 0:
                summed += entry_val
                nonzero_entries += 1
                # logger.info(data_dict)
                # logger.info(entry_val.to_bytes(2,'big'))

        logger.info(f"{name} has {summed} total remainders and {nonzero_entries} entries")

    def _read_digest(self):
        try:
            digest = self.interface.digest_get(1)
            # self.recieved_digests.append(digest)
            data_list = self.learn_filter.make_data_list(digest)
            self.total_received += len(data_list)
            for data in data_list:
                tuple_list = bytes(data["src_addr"].val)
                if not self.tuples:
                    self.tuples = {tuple_list}
                else:
                    self.tuples.add(tuple_list)

            self.recievedDigest += 1
            if self.recievedDigest % 1000 == 0:
                print(f"Received {self.recievedDigest} digests")

            self.hasFirstData = True
        except Exception as err:
            self.missedDigest += 1
            print(f"error reading digest {self.missedDigest}, {err} ", end="", flush=True)
            if self.hasFirstData and self.missedDigest >= 10:
                self.isRunning = False
                print("")
            time.sleep(0.1)


    def _get_FCM_counters(self):
        fcm_tables = []
        for name, table in self.fcm_tables.items():
            control_name = ""
            if "fcm" in name:
                control_name = "FcmEgress.fcmsketch"
                name = name.replace("fcm", "sketch_reg")
            else:
                control_name = "WaterfallIngress"

            print(f"[FCM] Load in all data from {control_name}.{name}")
            summed = 0
            nonzero_entries = 0
            data_table = table.entry_get(self.dev_tgt, [])
            entries = []
            for data, key in data_table:
                data_dict = data.to_dict()
                entry_val = sum(data_dict[f"{control_name}.{name}.f1"])
                entries.append(entry_val)
                if entry_val != 0:
                    summed += entry_val
                    nonzero_entries += 1
                    # print(data_dict[f"{control_name}.{name}.f1"])

            print(f"{name} has {summed} total count and {nonzero_entries} entries with a max of {max(entries)}")
            fcm_tables.append(entries)
        return fcm_tables
            

    def run(self):
        self.isRunning = True
        while self.isRunning:
            self._read_digest()

        print(f"Received {len(self.recievedDigest)} digest from switch")
        print(f"Received {len(self.tuples)} tuples from switch")
        for t in self.tuples:
            print(t.hex())
        parsed_digest = 0
        prev_tuple_len = 0
        # for digest in self.recieved_digests:
        #     data_list = self.learn_filter.make_data_list(digest)
        #     self.total_received += len(data_list)
        #     for data in data_list:
        #         tuple_list = bytes(data["src_addr"].val)
        #         if not self.tuples:
        #             self.tuples = {tuple_list}
        #         else:
        #             self.tuples.add(tuple_list)
        #
            # print(f"Found {len(data_list)} tuples with {len(self.tuples)} uniques")
            # print(f"{prev_tuple_len}; In total received {prev_tuple_len + len(data_list) - len(self.tuples)} tuples to many")
            # prev_tuple_len = len(self.tuples)

            # parsed_digest += 1
            # if parsed_digest % 1000 == 0:
            #     print(f"Parsed {parsed_digest} of {self.recievedDigest} digests; Current tuples {len(self.tuples)}")

        for tuple in self.tuples:
            for name, tables in self.table_dict.items():
                for t, loc in zip(tables, ["hi", "lo"]):
                    self.evalutateEntryInTable(t, f"{name}_{loc}", tuple)


    def verify(self, in_tuples, iters):
        print(f"[WaterfallFcm - verify] Calculate Waterfall F1-score...")
        true_pos = false_pos = true_neg =  false_neg = 0

        # Compare dataset tuples with Waterfall Tuples
        for tup in in_tuples.keys():
            if tup in self.tuples:
                true_pos += 1
            else:
                false_pos += 1

        # F1 Score
        recall = precision = f1 = 0.0
        recall = true_pos / (true_pos + false_pos)
        precision = 1.0;
        f1 = 2 * ((recall * precision) / (precision + recall))

        print(f"[WaterfallFcm - verify] {recall = :.5f} {precision = :.5f} | {f1 = :.5f}")

        load_factor = len(self.tuples) / len(in_tuples)
        print(f"[WaterfallFcm - verify] Load factor is {load_factor} with tuple having length {len(self.tuples)}")

        total_lf = self.total_received / len(in_tuples)
        print(f"[WaterfallFcm - verify] Total Load factor is {total_lf} with total received {self.total_received}")

        return
        print(f"[WaterfallFcm - verify] Estimate Flow Size Distribution")
        fcm_tables = self._get_FCM_counters()
        s1 = [fcm_tables[0], fcm_tables[3]]
        s2 = [fcm_tables[1], fcm_tables[4]]
        s3 = [fcm_tables[2], fcm_tables[5]]
        em_fsd = EM_WFCM(s1, s2, s3, self.tuples)

        for i in range(iters):
            self.ns = em_fsd.run_em(1)

            print(f"[WaterfallFcm - verify] Calculate Flow Size Distribution...")
            wmre = 0.0
            wmre_nom = 0.0
            wmre_denom = 0.0

            max_count_in = max(in_tuples.values())
            max_count_em = len(self.ns)
            print(f"[WaterfallFcm - verify] {max_count_in = } {max_count_em = }")

            max_count = max(max_count_in, max_count_em) + 1
            fsd = [0] * (max_count + 1)

            print(f"[WaterfallFcm - verify] Setup real EM...")
            for val in in_tuples.values():
                fsd[val] += 1

            print(f"[WaterfallFcm - verify] ...done")

            print(f"[WaterfallFcm - verify] Calculate WMRE...")
            for real, est in zip(fsd, self.ns):
                wmre_nom += abs(float(real) - est)
                wmre_denom += (float(real) + est) / 2

            if wmre_denom != 0:
                wmre = wmre_nom / wmre_denom

            print(f"[WaterfallFcm] WMRE : {wmre : .2f}")

        print(f"[WaterfallFcm] Finished EM FSD")

    def verify_sim(self, in_tuples, iters):
        print(f"[WaterfallFcm - verify sim] Estimate Flow Size Distribution")

        s1 = [[0] * W1, [0] * W1]
        s2 = [[0] * W2, [0] * W2]
        s3 = [[0] * W3, [0] * W3]

        print(f"[WaterfallFcm - verify sim] Setup sim stages")
        for d in range(DEPTH):
            for tup, val in in_tuples.items():
                if d == 0:
                    hash_idx = utils.crc32_sf(tup, 0xFFFFFFFF) % W1
                else:
                    hash_idx = utils.crc32_sf(tup, 0x0FFFFFFF) % W1
                
                if s1[d][hash_idx] + val < OVERFLOW_LEVEL1:
                    s1[d][hash_idx] += val
                    continue

                s1[d][hash_idx] = OVERFLOW_LEVEL1;
                hash_idx = int(hash_idx / 8)
                if s2[d][hash_idx] + val < OVERFLOW_LEVEL2:
                    s2[d][hash_idx] += val
                    continue

                s2[d][hash_idx] = OVERFLOW_LEVEL2
                hash_idx = int(hash_idx / 8)
                s3[d][hash_idx] += val;

        print(f"[WaterfallFcm - verify sim] Start estimation")


        em_fsd = EM_WFCM(s1, s2, s3, in_tuples)

        for i in range(iters):
            self.ns = em_fsd.run_em(1)

            print(f"[WaterfallFcm - verify sim] Calculate Flow Size Distribution...")
            wmre = 0.0
            wmre_nom = 0.0
            wmre_denom = 0.0

            max_count_in = max(in_tuples.values())
            max_count_em = len(self.ns)
            print(f"[WaterfallFcm - verify sim] {max_count_in = } {max_count_em = }")

            max_count = max(max_count_in, max_count_em) + 1
            fsd = [0] * (max_count + 1)

            print(f"[WaterfallFcm - verify sim] Setup real EM...")
            for val in in_tuples.values():
                fsd[val] += 1

            print(f"[WaterfallFcm - verify sim] ...done")

            print(f"[WaterfallFcm - verify sim] Calculate WMRE...")
            for real, est in zip(fsd, self.ns):
                wmre_nom += abs(float(real) - est)
                wmre_denom += (float(real) + est) / 2

            if wmre_denom != 0:
                wmre = wmre_nom / wmre_denom

            print(f"[WaterfallFcm] WMRE : {wmre : .6f}")

        print(f"[WaterfallFcm] Finished EM FSD")

def read_data_set(data_name):
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
    for t in tuples:
        print(t.hex())
    print("[Dataset Loader] Done!")
    return tuples


def main():
    parser = argparse.ArgumentParser(description="Loads in a dataset and connect to the network switch via gRPC. Verifies the results of the Waterfall and FCM sketch against the dataset")
    parser.add_argument('-i', '--input', type=str, required=True, help="Absolute path to dataset (/home/onie/*)")
    parser.add_argument('--sim',  action='store_true', help="Absolute path to dataset (/home/onie/*)")

    args = parser.parse_args()
    input_tuples = read_data_set(args.input)

    bfrt_interface = BfRt_interface(0, 'localhost:50052', 0)
    # bfrt_interface.list_tables()
    if args.sim:
        bfrt_interface.verify_sim(input_tuples, 1)
    else:
        bfrt_interface.run()
        bfrt_interface.verify(input_tuples, 2)

if __name__ == "__main__":
    main()
