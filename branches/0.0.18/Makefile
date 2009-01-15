ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC)
endif

.SUFFIXES:

include $(DEVKITPPC)/wii_rules

TARGET	= ftpii
SOURCES	= source
BUILD	= build
DATA	= data

CFLAGS				= -g -O2 -Wall $(MACHDEP) $(INCLUDE) -I$(LIBOGC_INC)
LDFLAGS				= -L$(LIBOGC_LIB) -lisfs -lnandimg -lfst -lwod -liso -ldi -lwiiuse -lbte -lfat -logc -lm -g $(MACHDEP) -Wl,-Map,$(notdir $@).map,--section-start,.init=0x80a00000
PRELOADER_LDFLAGS	= -L$(LIBOGC_LIB) -logc -g $(MACHDEP) -Wl,-Map,$(notdir $@).map

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:= $(CURDIR)/$(TARGET)
export VPATH	:= $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:= $(CURDIR)/$(BUILD)
export LD		:= $(CC)

BINFILES				:= $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
export OFILES			:= $(addsuffix .o,$(BINFILES)) common.o ftp.o loader.o vrt.o dol.o ftpii.o
export PRELOADER_OFILES	:= _$(TARGET).dol.o preloader.o dol.o

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).dol $(TARGET).elf

run:
	@wiiload $(TARGET).dol

else

$(OUTPUT).dol: $(OUTPUT).elf

$(OUTPUT).elf: $(PRELOADER_OFILES)
	@echo linking ... $(notdir $@)
	@$(LD) $^ $(PRELOADER_LDFLAGS) -o $@

%.dol.o: %.dol
	@bin2s $< | powerpc-gekko-as -o $@

_$(TARGET).elf: $(OFILES)

%.bin.o: %.bin
	@bin2s -a 32 $< | powerpc-gekko-as -o $@

DEPENDS = $(OFILES:.o=.d) $(PRELOADER_OFILES:.o=.d)
-include $(DEPENDS)

endif
