#! /usr/bin/env python3
import os, sys, subprocess
import mmap
import time
import struct
import multiprocessing as mp

from collections import defaultdict

from typing import Protocol
from EM_ctypes import EM_FSD

sys.path.append('/home/onie/sde/bf-sde-9.11.0/install/lib/python3.8/site-packages/tofino/')
sys.path.append('/home/onie/sde/bf-sde-9.11.0/install/lib/python3.8/site-packages/tofino/bfrt_grpc/')

# os.environ["GRPC_ENABLE_FORK_SUPPORT"] = "1"
os.environ["GRPC_POLL_STRATEGY"] = "poll"
# os.environ["GRPC_VERBOSITY"] = "debug"

import bfrt_grpc.client as gc


class BfRt_interface():

    def __init__(self, dev, grpc_addr, client_id):
        self.isRunning = False
        self.hasFirstData = False
        self.missedDigest = 0
        self.recievedDigest = 0
        self.recieved_datalist = []
        self.recieved_digests = []
        self.threads = []
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
        self.learn_filter.info.data_field_annotation_add("dst_addr", "ipv4")

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

        print("Connected to Device: {}, Program: {}, ClientId: {}".format(
                dev, self.p4_name, client_id))


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

    def _read_digest(self):
        try:
            digest = self.interface.digest_get(1)
            self.recieved_digests.append(digest)
            self.recievedDigest += 1
            self.hasFirstData = True
            if self.recievedDigest % 1000 == 0:
                print(f"{self.recievedDigest = }")
        except:
            self.missedDigest += 1
            # print(f"Received {len(data_list)} flows via digest, total {self.recievedDigest}")
            print(f"error reading digest {self.missedDigest}, total received {self.recievedDigest}", end=" ", flush=True)
            if self.missedDigest > 10 and self.hasFirstData:
                self.isRunning = False
                print("")

    def _parse_data_list(self, digest):
        data_list = self.learn_filter.make_data_list(digest)
        tuples = None
        for data in data_list:
            data_dict = data.to_dict()
            src_addr = data_dict["src_addr"]
            dst_addr = data_dict["dst_addr"]

            tuple_list = b''
            for val in src_addr.split("."):
                tuple_list += struct.pack("B", int(val))

            for val in dst_addr.split("."):
                tuple_list += struct.pack("B", int(val))

            tuple_list += data_dict["src_port"].to_bytes(2, 'big')
            tuple_list += data_dict["dst_port"].to_bytes(2, 'big')
            tuple_list += data_dict["protocol"].to_bytes(1, 'big')

            if not tuples:
                print(f"First tuple : {tuple_list}")
                tuples = {tuple_list}
            else:
                tuples.add(tuple_list)

        return tuples

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

            print(f"{name} has {summed} total remainders and {nonzero_entries} entries")
            fcm_tables.append(entries)
        return fcm_tables
            

    def run(self):
        self.isRunning = True
        while self.isRunning:
            self._read_digest()

        print(f"Received {self.recievedDigest} digests")
        print(f"Parallizing over 8 processes")
        with mp.Pool(processes=8) as pool:
            res = pool.map(self._parse_data_list, self.recieved_digests)
        print(res)
        self.tuples = res

        print(f"Received {len(self.tuples)} unique tuples from the switch")
        print(f"Closed process, start getting FCM counters")
        fcm_tables = self._get_FCM_counters()


        s1 = [fcm_tables[0], fcm_tables[3]]
        s2 = [fcm_tables[1], fcm_tables[4]]
        s3 = [fcm_tables[2], fcm_tables[5]]
        return [s1, s2, s3]

    def verify(self, in_tuples, stages):
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
        if true_pos == 0 and false_pos == 0:
            recall = 1.0;
        else:
            recall = true_pos / (true_pos + false_pos)
        
        if true_neg == 0 and false_neg == 0:
            precision = 1.0;
        else:
            precision = true_neg / (true_neg + false_neg)
        f1 = 2 * ((recall * precision) / (precision + recall))

        print(f"[WaterfallFcm - verify] {recall = :.3f} {precision = :.3f} | {f1 = :.3f}")

        load_factor = len(self.tuples) / len(in_tuples)
        print(f"[WaterfallFcm - verify] Load factor is {load_factor}")

        print("[WaterfallFcm] Start EM FSD...")
        em_fsd = EM_FSD(stages[0], stages[1], stages[2], self.tuples)
        self.ns = em_fsd.run_em(1)

        print(f"[WaterfallFcm - verify] Calculate Flow Size Distribution...")
        wmre = 0.0
        wmre_nom = 0.0
        wmre_denom = 0.0

        max_count_in = max(in_tuples.values())
        max_count_em = len(self.ns)
        print(f"[WaterfallFcm - verify] {max_count_in = } {max_count_em = }")

        max_count = max(max_count_in, max_count_em) + 1
        fsd = [0] * max_count

        print(f"[WaterfallFcm - verify] Setup real EM...")
        for val in in_tuples:
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

def read_data_set(data_name):
    print(f"[Dataset Loader] Get data from {data_name}")

    tuples = defaultdict(int)
    firstData = True
    with open(data_name, "r+b") as of:
        with mmap.mmap(of.fileno(), length=0, access=mmap.ACCESS_READ) as f:
            data = f.read()
        print(f"[Dataset Loader] Loaded in dataset into memory")
        for i in range(0, len(data), 13):
            if i + 12 >= len(data):
                break
            # Read src and dst addr
            tuples[data[i + 0:i + 13]] += 1

            if firstData:
                firstData = False
                print(data[i + 0: i + 13])

    # delay = 10
    # print(f"[Dataset Loader] ...done! Waiting for {delay}s before starting test...")
    # time.sleep(delay)
    print(f"[Dataset Loader] Parse data into tuples, done!")
    return tuples


def main():
    input_tuples = read_data_set("/home/onie/jboumans/equinix-chicago.20160121-130000.UTC.dat")
    bfrt_interface = BfRt_interface(0, 'localhost:50052', 0)
    # bfrt_interface.list_tables()

    val = os.fork()
    if val == 0:
        bfrt_interface._read_digest()
    else:
        stages = bfrt_interface.run()
        bfrt_interface.verify(input_tuples, stages)

if __name__ == "__main__":
    main()
