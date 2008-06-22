ftpii is an FTP server for the Nintendo Wii.


*** INSTALLATION ***

Copy ftpii/ to /apps on an SD-card and use The Homebrew Channel,
or load boot.elf using your favourite mechanism (wiiload, Twilight Hack, etc...).


*** THANKS ***

Thanks to those in EFnet #wiidev for all the help, particularly nilsk123 for his
persistent beta testing and suggestions, srg for his contributions, and all those
who help make devkitPPC, libogc, libfat and The Homebrew Channel the great
homebrew/development environment that it is.


*** TODO LIST *** (in no particular order):

 - read data and control connection at same time
 - multiple data connections for single client (?)
 - ABOR, STAT, HELP, FEAT
 - mem card slots, sd gecko, NAND, dvd (some of these may already/soon work thanks to libfat)
 - eliminate die() where possible
 - real auth
 - SITE LOAD (load a .dol or .elf)
 - allow server to start without a fat device
 - socket timeouts
 - use SO_REUSEADDR ?
 - async networking
 - nice UI ;-)


*** CONTACT ***

http://code.google.com/p/ftpii/

ftpii is written and maintained by Joe Jordan <joe.ftpii@psychlaw.com.au>
Device remounting support by Daniel Ehlers <danielehlers@mindeye.net>
Virtual path support derived from Daniel Ehlers' srg_vrt branch


*** HISTORY ***

For subversion changes since 0.0.6, see http://code.google.com/p/ftpii/source/list

20080622 0.0.7 Added virtual path support for /sd and /usb. (thanks srg!)
               Added SITE LOADER command to return to loader.
20080617 0.0.6 Incorporated SD-card and USB hot-swapping patch. (thanks srg!)
20080615 0.0.5 Added support for buggy FTP clients that use "LIST -aL" or similar, at the expense of breaking
               paths that begin with '-'.
               Compiled with corruption-fix and usbstorage libfat patches. (thanks svpe!)
               Uncommented FAT read-ahead support (oops)
20080613 0.0.4 Added rename support, as well as append and resume.
               Fixed _another_ nasty crash bug introduced in 0.0.3 when creating directories. (thanks nilsk123!)
20080612 0.0.3 Multi-client support (up to 5), using LWPs.  Unfortunately this makes things
               quite a bit slower, due to the reduced buffer sizes.  This has the nice
               side-effect of making clients like FileZilla work.
20080609 0.0.2 Fixed display/crash bug when creating directories. (thanks feesh!)
               Enabled fat read-ahead caching.  Increases read speed from ~80KB/s to ~250KB/s for me. (thanks svpe!)
20080608 0.0.1 Public release.
