#!/bin/bash
# TODO: describe me
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.
set -e

usage() { echo "Usage: $0 --{run, pull, build, publish, remove} [ --interactive-notebook-dir DIRECTORY ]  " 1>&2; exit 1; }
if [ "$#" -ne 1 -a "$#" -ne 3 ]; then usage; fi

NOTEBOOK_DIR="/usr/local/share/systemtap/interactive-notebook"
if [ "$2" = '--interactive-notebook-dir' ]; then
    NOTEBOOK_DIR=$3
    if [ ! -e $NOTEBOOK_DIR/isystemtap/constants.py ]; then
        echo "Usage: --interactive-notebook-dir $NOTEBOOK_DIR is not a valid installed interactive-notebook directory. It must contain constants.py" 1>&2; exit 1;
    fi
    echo "Using notebook dir: $NOTEBOOK_DIR"
elif [ -z "$2" ]; then
    echo "Using notebook dir: $NOTEBOOK_DIR"
else
    usage; exit 1
fi

REPO="quay.io"
IMAGE="rgoldber/isystemtap"
TAG="latest"

if [ "$1" = '--run' ]; then
    if [ $EUID -eq 0 ]; then
        # The container was started with sudo
        # Note: We could allow the user to run as root, but since they can install locally anyways we disallow it
        # here
        echo "Usage: The container cannot be run as root. If this is needed run ISystemtap locally" 1>&2; exit 1;
    fi

    HOST_USER=`whoami`
    # The notebook user, uid and gid
    NB_USER="jovyan"
    NB_UID=1000
    NB_GID=100

    # Extract the constants.py values, since the locally installed version of stap
    # is what the container needs (tr removes whitespace)
    STAP_VERSION_DEF=`grep VERSION $NOTEBOOK_DIR/isystemtap/constants.py | tr -d " \t\n\r"`
    STAP_PKGDATADIR_DEF=`grep PKGDATADIR $NOTEBOOK_DIR/isystemtap/constants.py | tr -d " \t\n\r"`
    STAP_PATH_DEF=`grep PATH $NOTEBOOK_DIR/isystemtap/constants.py | tr -d " \t\n\r"`
    # We also unpack the static dir containing the examples since
    # we need to mount that 
    STAP_PKGDATADIR=`echo "$STAP_PKGDATADIR_DEF" | tr -d "' " | awk -F "=" '{print $2}'`

    SUBUID_SIZE=$(( $(podman info --format "{{ range .Host.IDMappings.UIDMap }}+{{.Size }}{{end }}" ) - 1 ))
    SUBGID_SIZE=$(( $(podman info --format "{{ range .Host.IDMappings.GIDMap }}+{{.Size }}{{end }}" ) - 1 ))

    run_args=(
        # We need privileged network access to freely ssh/run websockets, etc.
        --privileged  \
        --net=host    \
        # MOUNTED VOLUMES ----------------------
        # Monitor mode interactions occur in /proc/systemtap/MODULE_NAME
        -v /proc:/proc:rw \
        # VERY IMPORTANT: The way in which the container communicates with the
        # host is via ssh, so we need to mount our .ssh dir within
        -v $HOME/.ssh:/home/$NB_USER/.ssh:rw \
        # The directory where we will find the examples, tapsets, etc.
        # If installed correctly it is the parent of NOTEBOOK_DIR
        -v $STAP_PKGDATADIR:$STAP_PKGDATADIR \
        # It's convinient to be able to access the host's home directory and working directory
        # We bring along Downlaods too since its often nice to have ~/Downloads where we expect it 
        -v $PWD:/home/$NB_USER/working_dir \
        -v $HOME/Downloads:/home/$NB_USER/Downloads \
        -v $HOME:/home/$NB_USER/${HOST_USER}_home_dir \
        # ENVIRONMENT VARIABLES ------------------
        -e hostuser=$HOST_USER \
        -e $STAP_VERSION_DEF \
        -e $STAP_PKGDATADIR_DEF \
        -e $STAP_PATH_DEF \
        # We remap the user/group namespaces so that jovyan and the current user are the same
        --user $NB_UID:$NB_GID \
        --uidmap $NB_UID:0:1 --uidmap 0:1:$NB_UID --uidmap $(($NB_UID+1)):$(($NB_UID+1)):$(($SUBUID_SIZE-$NB_UID)) \
        --gidmap $NB_GID:0:1 --gidmap 0:1:$NB_GID --gidmap $(($NB_GID+1)):$(($NB_GID+1)):$(($SUBGID_SIZE-$NB_GID))
    )
    podman run -it --rm --name isystemtap "${run_args[@]}" "$IMAGE:$TAG"
elif [ "$1" = '--pull' ]; then
    podman pull "$REPO/$IMAGE"
elif [ "$1" = '--build' ]; then
    # Build the image 
    podman build \
        --file "$NOTEBOOK_DIR/container/Dockerfile" \
        --format="docker" \
        --tag="$IMAGE" \
        "$NOTEBOOK_DIR/.."
elif [ "$1" = '--publish' ]; then
    podman login $REPO
    podman push $IMAGE $REPO/$IMAGE
elif [ "$1" = '--remove' ]; then
    podman image rm "$IMAGE:$TAG"
else
    usage;
fi
