#! /usr/bin/env python3
import os, sys  
from EM_ctypes import EM_FSD

sys.path.append('/home/onie/sde/bf-sde-9.11.0/install/lib/python3.8/site-packages/tofino/')
sys.path.append('/home/onie/sde/bf-sde-9.11.0/install/lib/python3.8/site-packages/tofino/bfrt_grpc/')

os.environ["GRPC_ENABLE_FORK_SUPPORT"] = "1"
os.environ["GRPC_POLL_STRATEGY"] = "poll"
os.environ["GRPC_VERBOSITY"] = "debug"


import bfrt_grpc.client as gc


class BfRt_interface():

    def __init__(self, dev, grpc_addr, client_id):
        self.isRunning = False
        self.missedDigest = 0
        self.tuples = {}

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
            data_list = self.learn_filter.make_data_list(digest)
            data_dict = data_list[0].to_dict()
            src_addr = data_dict["src_addr"]
            dst_addr = data_dict["dst_addr"]
            src_port = data_dict["src_port"]
            dst_port = data_dict["dst_port"]
            protocol = data_dict["protocol"]
            remain4 = data_dict["remain4"]
            print(f"{src_addr = } : {dst_addr = } | {src_port = } {dst_port = } | {protocol = } | {remain4}")

            raw_src_addr = [int(x) for x in src_addr.split('.')]
            raw_dst_addr = [int(x) for x in dst_addr.split('.')]
            raw_src_port = [int(x) for x in int(src_port).to_bytes(2, byteorder='big')]
            raw_dst_port = [int(x) for x in int(dst_port).to_bytes(2, byteorder='big')]
            raw_protocol = [int(protocol)]
            # print(f"{raw_src_addr = } : {raw_dst_addr = } | {raw_src_port = } {raw_dst_port = } | {raw_protocol = }")
            tuple_list = raw_src_addr + raw_dst_addr + raw_src_port + raw_dst_port + raw_protocol
            tuple_key = ".".join([str(x) for x in tuple_list])
            self.tuples[tuple_key] = tuple_list
        except:
            print("error reading digest", end="", flush=True)
            self.missedDigest += 0
        if self.missedDigest > 10:
            self.isRunning = False


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
        fcm_tables = self._get_FCM_counters()

        print("[WaterfallFcm] Start EM FSD...")
        s1 = [fcm_tables[0], fcm_tables[3]]
        s2 = [fcm_tables[1], fcm_tables[4]]
        s3 = [fcm_tables[2], fcm_tables[5]]
        em_fsd = EM_FSD(s1, s2, s3, self.tuples.values())
        ns = em_fsd.run_em(1)
        # print(fsd)
        # print(f"{ns[-1]} - sz {len(ns)}, {fsd[-1]} - sz {len(fsd)}")

        wmre_nom = 0.0
        wmre_denom = 0.0
        # for real, est in zip(fsd, ns):
        #     wmre_nom += abs(float(real) - est)
        #     wmre_denom += (float(real) + est) / 2

        wmre = wmre_nom / wmre_denom

        
        print(f"[WaterfallFcm] WMRE : {wmre : .2f}")
        print(f"[WaterfallFcm] Finished EM FSD")


def main():
    bfrt_interface = BfRt_interface(0, 'localhost:50052', 0)
    # bfrt_interface.list_tables()
    bfrt_interface.run()

if __name__ == "__main__":
    main()
