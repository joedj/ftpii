ftpii is an FTP server for the Nintendo Wii.


*** INSTALLATION ***

Copy ftpii/ to /apps/ on an SD-card and use The Homebrew Channel,
or load boot.elf using your favourite mechanism (wiiload, Twilight Hack, etc...).

To specify a password via The Homebrew Channel, rename the apps/ftpii directory to apps/ftpii_YourPassword.
To specify a password via wiiload, pass an argument e.g. wiiload boot.elf YourPassword.


*** THANKS ***

Thanks to those in EFnet #wiidev for all the help, particularly nilsk123 for his
persistent beta testing and suggestions, srg for his contributions, and all those
who help make devkitPPC, libogc, libfat and The Homebrew Channel the great
homebrew/development environment that it is.


*** TODO LIST *** (in no particular order):

 - read data and control connection at same time
 - multiple data connections for single client (?)
 - ABOR, STAT, HELP, FEAT
 - mem card slots, NAND, dvd
 - sd gecko (support is here but disabled in libfat)
 - eliminate die() where possible
 - SITE LOAD (load a .dol or .elf)
 - socket timeouts
 - use SO_REUSEADDR ?
 - async networking
 - nice UI ;-)


*** CONTACT ***

http://code.google.com/p/ftpii/

Contributors:
  ftpii is written and maintained by Joe Jordan <joe.ftpii@psychlaw.com.au>
  Daniel Ehlers <danielehlers@mindeye.net> makes regular source contributions


*** HISTORY ***

For subversion changes since 0.0.6, see http://code.google.com/p/ftpii/source/list

20080705 0.0.10 Added SITE PASSWD and SITE NOPASSWD for controlling the authentication remotely.
20080705 0.0.9  Added authentication - can specify a password using directory name or wiiload arg.
20080629 0.0.8  Added no-op SITE CHMOD command to prevent some FTP clients from displaying skip/abort/retry type prompts.
                Fixed MKD bug that caused working directory to change to new directory automatically.
20080624 0.0.7  Added virtual path support for /sd and /usb. (thanks srg!)
                Added SITE LOADER command to return to loader.
                Added SITE CLEAR command to clear the console.
                Support for starting without a device connected (thanks srg!)
                Support for SD Gecko (though currently disabled in libfat) (thanks srg!)
                Re-enable read-ahead when remounting (thanks srg!)
20080617 0.0.6  Incorporated SD-card and USB hot-swapping patch. (thanks srg!)
20080615 0.0.5  Added support for buggy FTP clients that use "LIST -aL" or similar, at the expense of breaking
                paths that begin with '-'.
                Compiled with corruption-fix and usbstorage libfat patches. (thanks svpe!)
                Uncommented FAT read-ahead support (oops)
20080613 0.0.4  Added rename support, as well as append and resume.
                Fixed _another_ nasty crash bug introduced in 0.0.3 when creating directories. (thanks nilsk123!)
20080612 0.0.3  Multi-client support (up to 5), using LWPs.  Unfortunately this makes things
                quite a bit slower, due to the reduced buffer sizes.  This has the nice
                side-effect of making clients like FileZilla work.
20080609 0.0.2  Fixed display/crash bug when creating directories. (thanks feesh!)
                Enabled fat read-ahead caching.  Increases read speed from ~80KB/s to ~250KB/s for me. (thanks svpe!)
20080608 0.0.1  Public release.
