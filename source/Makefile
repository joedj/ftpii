ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC)
endif

include $(DEVKITPPC)/wii_rules

.PHONY = all

TARGET = ftpii
DEPSDIR = .
CFLAGS = -g -O2 -Wall $(MACHDEP) $(INCLUDE) -I$(LIBOGC_INC)

FTPII_OBJS = ftpii.o common.o ftp.o loader.o vrt.o dol.o
FTPII_DEPENDS = $(FTPII_OBJS:.o=.d)

PRELOADER_OBJS = _$(TARGET).dol.o preloader.o dol.o
PRELOADER_LDFLAGS = -L$(LIBOGC_LIB) -logc -g $(MACHDEP) -Wl,-Map,$(notdir $@).map
PRELOADER_DEPENDS := $(PRELOADER_OBJS:.o=.d)

LDFLAGS = -L$(LIBOGC_LIB) -lisfs -lnandimg -lfst -lwod -liso -ldi -lwiiuse -lbte -lfat -logc -lm -g $(MACHDEP) -Wl,-Map,$(notdir $@).map,--section-start,.init=0x80a00000

export LD := $(CC)

all: $(TARGET).dol

$(TARGET).elf: $(PRELOADER_OBJS)
	@echo linking ... $@
	@$(LD) $^ $(PRELOADER_LDFLAGS) -o $@

_$(TARGET).dol.o: _$(TARGET).dol _$(TARGET).dol.s

_$(TARGET).elf: $(FTPII_OBJS)

%.dol.s: %.dol
	@bin2s $< > $@

clean:
	@rm -f *.o *.d *.map *.s *.dol *.elf

run:
	@wiiload $(TARGET).dol

-include $(FTPII_DEPENDS)
-include $(PRELOADER_DEPENDS)
