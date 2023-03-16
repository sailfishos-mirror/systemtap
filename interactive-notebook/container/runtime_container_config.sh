#!/bin/bash
# A runtime config for the isystemtap container
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

localhost=127.0.0.1
ssh_dir="$HOME/.ssh"

echo $ssh_dir
if [ ! -f $ssh_dir/id_rsa.pub ]; then
    echo "An ssh public key is required! Create one with ssh-keygen";
    exit 1
fi

if ! grep -qs "Host $localhost" $ssh_dir/config
then
    # Probably the first time running this container
    echo "Setting up ssh from jupyter-container to localhost for systemtap remote connection"
    # Add an entry to authorized_keys allowing passwordless-ssh to localhost
    # No security-risk since we're already logged
    ssh-copy-id -i $ssh_dir/id_rsa.pub $hostuser@$localhost
    # Further remove first-time key authentication and remove the need for ssh $hostuser@localhost
    echo -e "Host $localhost\n\tStrictHostKeyChecking no\n\tUser $hostuser\n" >> $ssh_dir/config
fi
# It is already installed, so we need to find that location and add the remote option as well as the HOST's config
INSTALL_PATH=`python -c 'import os; import isystemtap; print(os.path.abspath(isystemtap.__path__[-1]))'`
echo "STAP_VERSION    = $STAP_VERSION"    >  $INSTALL_PATH/constants.py
echo "STAP_PKGDATADIR = $STAP_PKGDATADIR" >> $INSTALL_PATH/constants.py
echo "STAP_PATH       = $STAP_PATH"       >> $INSTALL_PATH/constants.py
echo "SSH_DEST        = '$localhost'"     >> $INSTALL_PATH/constants.py