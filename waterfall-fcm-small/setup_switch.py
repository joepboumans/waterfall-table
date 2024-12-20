from ipaddress import ip_address
import time

p4 = bfrt.waterfall_fcm.pipe

def get_pg_info(dev_port, queue_id):
   pipe_num = dev_port >> 7
   entry = bfrt.tf1.tm.port.cfg.get(dev_port, print_ents=False)
   pg_id = entry.data[b'pg_id']
   pg_queue = entry.data[b'egress_qid_queues'][queue_id]

   print('DEV_PORT: {} QueueID: {} --> Pipe: {}, PG_ID: {}, PG_QUEUE: {}'.format(dev_port, queue_id, pipe_num, pg_id, pg_queue))

   return pipe_num, pg_id, pg_queue

# Clear All tables
def clear_all():
    global p4

    # The order is important. We do want to clear from the top, i.e.
    # delete objects that use other objects, e.g. table entries use
    # selector groups and selector groups use action profile members

    # Clear Match Tables
    for table in p4.info(return_info=True, print_info=False):
        if table['type'] in ['MATCH_DIRECT', 'MATCH_INDIRECT_SELECTOR']:
            print("Clearing table {}".format(table['full_name']))
            for entry in table['node'].get(regex=True):
                entry.remove()
    # Clear Selectors
    for table in p4.info(return_info=True, print_info=False):
        if table['type'] in ['SELECTOR']:
            print("Clearing ActionSelector {}".format(table['full_name']))
            for entry in table['node'].get(regex=True):
                entry.remove()
    # Clear Action Profiles
    for table in p4.info(return_info=True, print_info=False):
        if table['type'] in ['ACTION_PROFILE']:
            print("Clearing ActionProfile {}".format(table['full_name']))
            for entry in table['node'].get(regex=True):
                entry.remove()

    # Clearing register
    for table in p4.info(return_info=True, print_info=False):
        if table['type'] in ['REGISTER']:
            print("Clearing Regitser {}".format(table['full_name']))
            table['node'].clear()

clear_all()

#bfrt.tf1.tm.port.sched_shaping.mod(dev_port=132, unit='BPS', provisioning='MIN_ERROR', max_rate=150000000, max_burst_size=160)
#bfrt.tf1.tm.port.sched_shaping.mod(dev_port=140, unit='BPS', provisioning='MIN_ERROR', max_rate=15000000, max_burst_size=160)

# bfrt.tf1.tm.port.sched_cfg.mod(dev_port=5, max_rate_enable=True)
# bfrt.tf1.tm.port.sched_cfg.mod(dev_port=3, max_rate_enable=True)
#
# bfrt.tf1.tm.port.sched_cfg.mod(dev_port=5, max_rate_enable=True)
# bfrt.tf1.tm.port.sched_cfg.mod(dev_port=3, max_rate_enable=True)
# bfrt.tf1.tm.port.sched_shaping.mod(dev_port=3, unit='PPS', provisioning='MIN_ERROR', max_rate=1, max_burst_size=1)
# bfrt.tf1.tm.port.sched_shaping.mod(dev_port=5, unit='PPS', provisioning='MIN_ERROR', max_rate=1, max_burst_size=1)

#bfrt.tf1.tm.port.sched_cfg.mod(dev_port=5, max_rate_enable=True)
#bfrt.tf1.tm.port.sched_cfg.mod(dev_port=3, min_rate_enable=True)
#bfrt.tf1.tm.port.sched_cfg.mod(dev_port=5, min_rate_enable=True)
# bfrt.tf1.tm.port.sched_shaping.mod(dev_port=3, unit='PPS', provisioning='MIN_ERROR', max_rate=1, max_burst_size=1)
# bfrt.tf1.tm.port.sched_shaping.mod(dev_port=5, unit='PPS', provisioning='MIN_ERROR', max_rate=1, max_burst_size=1)

for port_number in [132, 140, 148, 156]:
    for queue_id in range(8):
        pipe_num, pg_id, pg_queue=get_pg_info(port_number, queue_id)
        bfrt.tf1.tm.queue.sched_cfg.mod(pipe=pipe_num, pg_id=pg_id, pg_queue=pg_queue, min_priority=queue_id,max_priority=queue_id)

print("populating forward table...")
forward_tbl = p4.WaterfallIngress.forward
forward_tbl.add_with_hit(ingress_port=132, dst_port=132)
forward_tbl.add_with_hit(ingress_port=140, dst_port=140)
forward_tbl.add_with_hit(ingress_port=148, dst_port=148)
forward_tbl.add_with_hit(ingress_port=156, dst_port=156)
forward_tbl.add_with_no_action(resubmit_flag=0x1)

print("populating resub table...")
resub = p4.WaterfallIngress.resub
resub.add_with_no_action(found=False, resubmit_flag=0x0)
resub.add_with_no_action(found=True, resubmit_flag=0x0)
resub.add_with_no_action(resubmit_flag=0x1)

print("populating swaps table...")
swap1 = p4.WaterfallIngress.swap1
swap1.add_with_lookup1(resubmit_flag=0x0)
swap1.add_with_do_swap1(resubmit_flag=0x1)

swap2 = p4.WaterfallIngress.swap2
swap2.add_with_lookup2(resubmit_flag=0x0)
swap2.add_with_do_swap2(resubmit_flag=0x1, out_remain1_start=0x1, out_remain1_end=0xFFFF)

swap3 = p4.WaterfallIngress.swap3
swap3.add_with_lookup3(resubmit_flag=0x0)
swap3.add_with_do_swap3(resubmit_flag=0x1, out_remain2_start=0x1, out_remain2_end=0xFFFF)

swap4 = p4.WaterfallIngress.swap4
swap4.add_with_lookup4(resubmit_flag=0x0)
swap4.add_with_do_swap4(resubmit_flag=0x1, out_remain3_start=0x1, out_remain3_end=0xFFFF)

# prt = bfrt.port.port
print("activating ports...")
bfrt.port.port.add(DEV_PORT=132, SPEED='BF_SPEED_100G', FEC='BF_FEC_TYP_REED_SOLOMON', PORT_ENABLE=True)
bfrt.port.port.add(DEV_PORT=140, SPEED='BF_SPEED_100G', FEC='BF_FEC_TYP_REED_SOLOMON', PORT_ENABLE=True)
bfrt.port.port.add(DEV_PORT=148, SPEED='BF_SPEED_100G', FEC='BF_FEC_TYP_REED_SOLOMON', PORT_ENABLE=True)
bfrt.port.port.add(DEV_PORT=156, SPEED='BF_SPEED_100G', FEC='BF_FEC_TYP_REED_SOLOMON', PORT_ENABLE=True)
#prt.add(DEV_PORT=0, SPEED="BF_SPEED_40G", FEC="BF_FEC_TYP_FC", PORT_ENABLE=1) # port x
#prt.add(DEV_PORT=440, SPEED="BF_SPEED_40G", FEC="BF_FEC_TYP_FC", PORT_ENABLE=1) # port x

# Throttling
bfrt.tf1.tm.port.sched_cfg.mod(dev_port=140, max_rate_enable=True)
bfrt.tf1.tm.port.sched_shaping.mod(dev_port=132, unit='BPS', provisioning='MIN_ERROR', max_rate=9000000, max_burst_size=160)
bfrt.tf1.tm.port.sched_shaping.mod(dev_port=140, unit='BPS', provisioning='MIN_ERROR', max_rate=9000000, max_burst_size=160)
#bfrt.tf1.tm.port.sched_shaping.mod(dev_port=148, unit='PPS', provisioning='MIN_ERROR', max_rate=12, max_burst_size=2)
#bfrt.tf1.tm.port.sched_shaping.mod(dev_port=156, unit='PPS', provisioning='MIN_ERROR', max_rate=12, max_burst_size=2)


bfrt.complete_operations()

#pg_ids = [3,7]
#while True:
#    for pg_id in pg_ids:
#        for i in range(8):
#            start_time = time.time_ns()
#            bfrt.tf1.tm.counter.queue.get(pipe=1, pg_id=pg_id, pg_queue=i, from_hw=True)
#            end_time = time.time_ns()
#            print(f"reading queue {i} in port {pg_id} took {end_time - start_time} ns")



# # Final programming
# print("""
# # ******************* PROGAMMING RESULTS *****************
# # """)
# print ("Table ipv4_lpm:")
# ipv4_lpm.dump(table=True)
# print ("Table forward:")
# forward.dump(table=True)

