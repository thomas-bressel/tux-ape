TuxAPE
======

An Amstrad CPC and Plus emulator for Linux, modelled on WinAPE 2.0 Beta 2.
(LISEZMOI.txt says the same in French.)


What it needs
-------------

A 64-bit Linux for PCs (x86-64), from 2022 or later: Ubuntu 22.04, Linux
Mint 21, Debian 12, Fedora 36, or what came after them. Nothing to install:
Qt, the library of TuxAPE's windows, is in the qt folder. Sound goes through
PipeWire, PulseAudio or ALSA, which every desktop Linux has.


Starting it
-----------

    ./tuxape
    ./tuxape game.dsk       the disc goes into drive A:

or double-click the tuxape file.

The help is in the program: the Help menu, or the F1 key. The windows also
exist in French: Settings > Language.


The folders
-----------

    roms         the ROMs. The machines' own in system, the firmware in
                 other languages in firmware, ParaDOS in dos, the Multiface
                 II in tools, the DMP2000 printer's typefaces in printer.
                 Your own ROMs go anywhere in this folder: TuxAPE finds them
                 by their names.
    profile      the profiles (.tpf), from the CPC 464 to the 6128 Plus
    library      your games and programs, for the Library window (F4). In
                 Utilities, the discs that install the software of the
                 X-MEM and X-MASS cards, the iMPsuite and Orgams.
    img          the X-MEM's flash memory and the X-MASS's disc, blank, to
                 be installed as on a real CPC
    dsk          your discs
    assembly     your sources for the assembler
    savestates   the quick saves (F5 saves, F6 takes back)
    plugins      the plug-ins, installed from Settings > Plugins
    qt           Qt

The settings are kept in ~/.config/TuxAPE.

Installed by the package (tuxape_*.deb), TuxAPE is in /opt/tuxape, where
only the system writes: your discs, quick saves, profiles, Library, sources
and plug-ins are then in the TuxAPE folder of your home, made the first
time with what TuxAPE comes with. Your own ROMs go in ~/TuxAPE/roms.
Removing TuxAPE leaves that folder where it is.


If TuxAPE does not start
------------------------

Start it from a terminal: it says what it lacks. For the details of how its
windows are loaded:

    QT_DEBUG_PLUGINS=1 ./tuxape


Credits
-------

TuxAPE © Thomas Bressel 2026. Version 1.1. Licence of use: LICENCE.txt.

WinAPE, the model of this emulator, is the work of Richard Wilson. TuxAPE
takes up how it works and its file formats, not its code.

Technical information from the "Amstrad CPC CRTC Compendium" by Longshot /
Logon System (CC BY-NC-ND). Tests on real machines by Kevin Thacker (the
"acid tests") and Longshot (the Shaker).

TuxAPE contains Qt 6.10 (LGPL version 3 licence, whose text is in
qt/LICENSES; its sources are at https://download.qt.io), SDL 2 (zlib
licence) and the xcb-util libraries (MIT licence). It uses your system's
zlib (zlib licence). Qt's libraries are in qt/lib: they can be replaced by
other versions of Qt 6.
