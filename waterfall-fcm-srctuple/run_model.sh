#!/bin/bash

sudo $SDE_INSTALL/bin/veth_setup.sh
$SDE/run_tofino_model.sh -p waterfall_fcm -c build/p4/waterfall_fcm/tofino/waterfall_fcm.conf --log-dir log
