#!/bin/bash
# An install script for isystemtap and its jupyterlab extensions
# NB: Should be run from interactive-notebook
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

type python >/dev/null || { echo "python required"; exit 1; }
type pip    >/dev/null || { echo "pip required";    exit 1; }
type npm    >/dev/null || { echo "npm required";    exit 1; }

echo "Installing requirements"
if command -v mamba &> /dev/null
then
    # The jupyter container images use mamba, so we follow suit
    mamba install -yc kirill-odc --file requirements.txt
    mamba clean --all -f -y 
else
    # Otherwise just default to pip
    pip install -U --user --no-cache-dir -r requirements.txt
fi

echo
echo "Installing Jupyter kernelspec"
jupyter kernelspec install --user isystemtap

echo
echo "Installing ISystemtap"
# Installs the setup.py
pip install .

echo
extenstion="jupyterlab-stap-highlight"
echo "Checking $extenstion extenstion installation"
if jupyter labextension check $extenstion --installed 2> /dev/null; then
    echo "$extenstion already installed"
else
    (cd codemirror; npm install && jupyter labextension link --dev-build=False .)
    echo "$extenstion installed"
fi

echo
echo "Setting up language server"
lsp_config=jupyter_stap_lsp.json
jconfig=`jupyter --config`
mkdir -p "$jconfig/jupyter_server_config.d/"
if [ "$1" = '--container-install' ]; then
    # NB: We preemptively add the config file, since we later only allow running the container with a lang-server enabled version of stap
    jq '.LanguageServerManager.language_servers."stap-language".argv = ["ssh", "127.0.0.1", "stap --language-server"]' $lsp_config > "$jconfig/jupyter_server_config.d/$lsp_config"
elif stap --help | grep language-server > /dev/null; then
    cp -v $lsp_config "$jconfig/jupyter_server_config.d/$lsp_config"
else
    echo "This version of systemtap does not support language-server mode. Skipping"
fi