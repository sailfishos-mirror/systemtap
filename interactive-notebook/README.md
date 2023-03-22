# ISystemtap
Welcome to ISystemtap, to get started check of the [usage](#usage) below and tryout the ISystemtap.ipynb

## Usage
### Local usage
Once ISystemtap is installed just run `jupyter-lab [notebook_name.ipynb]` and then select a new or existing ISystemtap notebook. For installation see [below](#local-installation)

### Container usage
Run `interactive-notebook/container/isystemtap_launcher.sh` (which under the default systemtap installation will be in `/usr/local/share/systemtap/`).
```bash
./isystemtap_launcher.sh --{run, pull, build, publish, remove} [ --interactive-notebook-dir DIRECTORY ]
```
* --run: Runs the image, if not pulled (or built locally) pull it from `quay.io`
* --pull: Pull the build image from `quay.io/rgoldber/isystemtap`
* --build: Do a local container build
* --publish [For maintainers only]: Publish the image to the public repo
* --remove: Remove the localy copy of the image
* --interactive-notebook-dir [OPTIONAL]: The directory where interactive-notebook can be found (defaults to `/usr/local/share/systemtap/interactive-notebook`)

## Local Installation
From `interactive-notebook/` run `./install.sh`. This will install the ISystemtap kernel, syntax highlighting and language-server dependencies in `~/.systemtap/jupyter`.

## FAQ
1. What if something goes wrong, and a systemtap kernel module is left running after ISystemtap shuts down
    * All you need to do is run `sudo staprun -d MODULE_NAME` (this can be checked in `/proc/systemtap`)