sudo sh -c 'echo "#Enable huge pages support for DMA purposes" >> /etc/sysctl.conf'
sudo sh -c 'echo "vm.nr_hugepages = 196" >> /etc/sysctl.conf'
echo "Setting up DMA Memory Pool"
hp=$(sudo sysctl -n vm.nr_hugepages)

if [ $hp -lt 196 ]; then
    if [ $hp -eq 0 ]; then
        add_hugepage
    else
        nl=$(egrep -c vm.nr_hugepages /etc/sysctl.conf)
        if [ $nl -eq 0 ]; then
            add_hugepage
        else
            sudo sed -i 's/vm.nr_hugepages.*/vm.nr_hugepages = 196/' /etc/sysctl.conf
        fi
    fi
    sudo sysctl -p /etc/sysctl.conf
fi

export LD_LIBRARY_PATH=$SDE_INSTALL/lib:$LD_LIBRARY_PATH:/usr/local/lib

sudo env "SDE=$SDE" "SDE_INSTALL=$SDE_INSTALL" "PATH=$PATH" \
      "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" build/bin/main "$@"

