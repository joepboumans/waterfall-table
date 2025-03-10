#!/bin/bash
set -e

if [ -z "${SDE}" ]; then
    echo "SDE is not set, please set this to P4 Studio folder and run this script again"
    exit 1
fi
echo "SDE is set"

if [ -z "${SDE_INSTALL}" ]; then
    echo "SDE_INSTALL is not set, please set this to P4 Studio install folder (typically: $SDE/install) and run this script again"
    exit 1
fi
echo "SDE_INSTALL is set"

echo "Creating build folder if it does not exist..."
mkdir -p build
