#!/bin/bash
# A runtime config for the isystemtap container
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

# We need to get some of the info from the host's version of systemtap (at runtime) and add it here
# ISystemtap is already installed, so we need to find that location and add the ssh dest
localhost=127.0.0.1
INSTALL_PATH=`python -c 'import os; import isystemtap; print(os.path.abspath(isystemtap.__path__[-1]))'`
echo "STAP_VERSION    = $STAP_VERSION"    >  $INSTALL_PATH/constants.py
echo "STAP_PKGDATADIR = $STAP_PKGDATADIR" >> $INSTALL_PATH/constants.py
echo "SSH_DEST        = '$localhost'"     >> $INSTALL_PATH/constants.py