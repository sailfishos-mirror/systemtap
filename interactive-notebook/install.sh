#!/bin/bash
type python >/dev/null || { echo "python required"; exit 1; }
type pip    >/dev/null || { echo "pip required";    exit 1; }
type npm    >/dev/null || { echo "npm required";    exit 1; }

echo "Installing requirements"
pip install --user -r requirements.txt

echo
echo "Installing Jupyter kernelspec"
jupyter kernelspec install --user isystemtap

echo
extenstion="jupyterlab-stap-highlight"
echo "Checking $extenstion extenstion installation"
if jupyter labextension check $extenstion --installed 2> /dev/null; then
    echo "$extenstion already installed"
else
    (cd codemirror; npm install && jupyter labextension link .)
    echo "$extenstion installed"
fi

echo
echo "Setting up language server"
if stap --help | grep language-server > /dev/null; then
    lsp_config=jupyter_stap_lsp.json
    jconfig=`jupyter --config`
    cp -v $lsp_config "$jconfig/jupyter_server_config.d/$lsp_config"
else
    echo "This version of systemtap does not support language-server mode. Skipping"
fi