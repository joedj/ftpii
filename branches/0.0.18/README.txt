
ftpii is an FTP server for the Nintendo Wii.


*** USAGE ***

Copy ftpii/ to /apps/ on an SD-card and use The Homebrew Channel,
or load boot.elf using your favourite mechanism (wiiload, Twilight Hack, ISO etc...).

To specify a password via The Homebrew Channel, rename the apps/ftpii directory to apps/ftpii_YourPassword.
To specify a password via wiiload, pass an argument e.g. wiiload boot.elf YourPassword.
To specify a password remotely, use the SITE PASSWD and SITE NOPASSWD commands.

A working DVDx installation is required for the DVD features.


*** THANKS ***

Thanks to those in EFnet #wiidev for all the help, particularly nilsk123 for his
persistent beta testing and suggestions, Daniel Ehlers (srg) for his contributions,
and all those who help make devkitPPC, libogc, libfat and The Homebrew Channel the great
homebrew/development environment that it is.


*** TODO LIST *** (in no particular order):

 - read data and control connection at same time
 - multiple data connections for single client (?)
 - ABOR, STAT, HELP, FEAT
 - gamecube memory cards
 - NAND filesystem
 - eliminate die() where possible
 - SITE LOAD (load a .dol or .elf)
 - nice UI ;-)


*** CONTACT ***

http://code.google.com/p/ftpii/

ftpii is written and maintained by Joe Jordan <joe.ftpii@psychlaw.com.au>
joedj @ EFNet #wiidev


*** HISTORY ***

For subversion changes since 0.0.6, see http://code.google.com/p/ftpii/source/list

20090105 0.0.18 Enabled SD Gecko at /gcsda and /gcsdb. (thanks dhewg!)
                Added NAND image support at /nand (libnandimg). 
20081230 0.0.17 Upgraded to devkitPPC r16.
                Fixed crash bug when mounting /fst for discs with fst_size greater than 32KB.
                Fixed logic bug where some files under /fst appear in the wrong directories.
                Fixed display initialisation bug causing intermittent blank display on startup.
                Added SITE LOAD command to run DOL executables. (thanks svpe/shagkur!)
                Added virtual metadata directories under /fst.
20081126 0.0.16 Added support for wiimote power button.
                Added SITE MOUNT and SITE UNMOUNT commands.
                Added SITE EJECT command to eject DVD.
                Added ISO9660 DVD support (libiso).
                Added Wii Optical Disc image support (libwod).
                Added Wii disc filesystem support (libfst). (thanks Nuke!)
20081026 0.0.15 boot.dol rebuilt with latest libogc git to add SDHC support. (thanks svpe!)
                Released as boot.dol instead of boot.elf for Homebrew Channel beta9 compatibility.
                Network initialisation is more reliable - now retries forever on net_init or net_gethostip failure.
20080918 0.0.14 boot.elf rebuilt with latest libfat CVS to fix delete corruption bug
                and speed up opening large files. (thanks rodries!)
20080816 0.0.13 Replaced threads with mostly-async networking.
                Attempt to detect whether to exit to loader or system menu (e.g. when loaded from a DVD).
                Clean up open descriptors before exiting.
                Added 30 second data connection timeout.
                Added support for power button.
                Added GameCube controller support.
20080726 0.0.12 boot.elf rebuilt with patched libfat to set archive flag on new files.,
                allowing Data Management to see uploaded save games (e.g. TP hack).
20080720 0.0.11 boot.elf rebuilt with patched libfat to stop read-ahead cache providing old data. (thanks dhewg!)
                Attempt to fix USB ethernet adapter by initialisaing network subsystem before FAT.
                Added release_date to meta.xml.
20080718 0.0.10 boot.elf rebuilt with patched libogc to fix startup crashes when USB devices are present.
                Added SITE PASSWD and SITE NOPASSWD for controlling the authentication remotely.
20080705 0.0.9  Added authentication - can specify a password using directory name or wiiload arg.
20080629 0.0.8  Added no-op SITE CHMOD command to prevent some FTP clients from displaying skip/abort/retry type prompts.
                Fixed MKD bug that caused working directory to change to new directory automatically.
20080624 0.0.7  Added virtual path support for /sd and /usb. (thanks srg!)
                Added SITE LOADER command to return to loader.
                Added SITE CLEAR command to clear the console.
                Support for starting without a device connected. (thanks srg!)
                Support for SD Gecko (though currently disabled in libfat). (thanks srg!)
                Re-enable read-ahead when remounting. (thanks srg!)
20080617 0.0.6  Incorporated SD-card and USB hot-swapping patch. (thanks srg!)
20080615 0.0.5  Added support for buggy FTP clients that use "LIST -aL" or similar, at the expense of breaking
                paths that begin with '-'.
                Compiled with corruption-fix and usbstorage libfat patches. (thanks svpe!)
                Uncommented FAT read-ahead support (oops).
20080613 0.0.4  Added rename support, as well as append and resume.
                Fixed _another_ nasty crash bug introduced in 0.0.3 when creating directories. (thanks nilsk123!)
20080612 0.0.3  Multi-client support (up to 5), using LWPs.  Unfortunately this makes things
                quite a bit slower, due to the reduced buffer sizes.  This has the nice
                side-effect of making clients like FileZilla work.
20080609 0.0.2  Fixed display/crash bug when creating directories. (thanks feesh!)
                Enabled fat read-ahead caching.  Increases read speed from ~80KB/s to ~250KB/s for me. (thanks svpe!)
20080608 0.0.1  Public release.
