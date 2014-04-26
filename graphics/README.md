Manic Miner graphics
====================

This directory contains the build script for Manic Miner's graphics.

To build the graphics, run:

    python makeall.py

This builds "manicminer.h" in the "manicminer/" directory.

You should have already installed the Python Gameduino 2 package https://pypi.python.org/pypi/gameduino2.

For maximum authenticity, the build script works by examining a snapshot of ZX Spectrum RAM ``mm2.z80`` and using the description at http://www.icemark.com/dataformats/manic/mmformat.htm to decode the graphics, audio and maps.
