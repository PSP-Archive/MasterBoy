set path=c:\devkitPro\devkitPSP\bin;c:\devkitPro\msys\bin;%path%
make -f makefile.psp
copy EBOOT.PBP F:\PSP\GAME\MMSPlus
@rem C:\devkitPro\psplink_2.0\PC\SendPspCommand ./MMSPlus.elf
