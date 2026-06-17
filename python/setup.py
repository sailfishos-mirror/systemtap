# systemtap python module setup file
# Copyright (C) 2016 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

from setuptools import setup, Extension
from setuptools.command.egg_info import egg_info
from setuptools.command.install_egg_info import install_egg_info
import os

# With the 'EggInfoCommand' class, we wrap the standard 'egg_info'
# class and override the build directory, which is normally the source
# directory. This is necessary since we're doing a VPATH build.
class EggInfoCommand(egg_info):
    def run(self):
        if "build" in self.distribution.command_obj:
            build_command = self.distribution.command_obj["build"]
            self.egg_base = build_command.build_base
            self.egg_info = os.path.join(self.egg_base,
                                         os.path.basename(self.egg_info))
        egg_info.run(self)

# See discussion above about the 'EggInfoCommand' class.
class InstallEggInfoCommand(install_egg_info):
    def run(self):
        if "build" in self.distribution.command_obj:
            build_command = self.distribution.command_obj["build"]
            self.source = os.path.join(build_command.build_base,
                                       os.path.basename(self.source))
        install_egg_info.run(self)

setup(
    ext_modules = [
        Extension("HelperSDT._HelperSDT", ["HelperSDT/_HelperSDT.c"]),
    ],
    cmdclass={
        "egg_info": EggInfoCommand,
        "install_egg_info": InstallEggInfoCommand,
    },
)
