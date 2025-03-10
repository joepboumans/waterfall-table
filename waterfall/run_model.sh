#!/bin/bash

sudo $SDE_INSTALL/bin/veth_setup.sh
$SDE/run_tofino_model.sh -p waterfall -c build/p4/waterfall/tofino/waterfall.conf --log-dir log
