ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC)
endif

include $(DEVKITPPC)/wii_rules

.PHONY = all

DEPSDIR = .
CFLAGS = -g -O2 -Wall $(MACHDEP) $(INCLUDE) -I$(LIBOGC_INC)

NESTED_OBJS = ftpii.o common.o ftp.o loader.o vrt.o dol.o
NESTED_DEPENDS = $(NESTED_OBJS:.o=.d)

FTPII_OBJS = nested.dol.o preloader.o dol.o
FTPII_LDFLAGS = -L$(LIBOGC_LIB) -logc -g $(MACHDEP) -Wl,-Map,$(notdir $@).map
FTPII_DEPENDS := $(FTPII_OBJS:.o=.d)

LDFLAGS = -lfst -lwod -liso -ldi -lwiiuse -lbte -lfat -lm $(FTPII_LDFLAGS),--section-start,.init=0x81330000

export LD := $(CC)

all: ftpii.dol

ftpii.elf: $(FTPII_OBJS)
	@echo linking ... $@
	@$(LD) $^ $(FTPII_LDFLAGS) -o $@

nested.dol.o: nested.dol.s

nested.elf: $(NESTED_OBJS)

%.dol.s: %.dol
	@bin2s $< > $@

clean:
	@rm -f *.o *.d *.map *.s *.dol *.elf

-include $(NESTED_DEPENDS)
-include $(FTPII_DEPENDS)
