#!/bin/bash

sudo $SDE_INSTALL/bin/veth_setup.sh
$SDE/run_tofino_model.sh -p simple_digest -c build/p4/simple_digest/tofino/simple_digest.conf --log-dir log
