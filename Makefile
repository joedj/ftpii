ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC)
endif

.SUFFIXES:

include $(DEVKITPPC)/wii_rules

TARGET	= ftpii
SOURCES	= source
BUILD	= build

CFLAGS				= -g -O2 -Wall $(MACHDEP) $(INCLUDE)
LDFLAGS				= -L$(LIBOGC_LIB) -lisfs -lnandimg -lfst -lwod -liso -ldi -lwiiuse -lbte -lfat -logc -lm -g $(MACHDEP) -Wl,-Map,$(notdir $@).map,--section-start,.init=0x80a00000
PRELOADER_LDFLAGS	= -L$(LIBOGC_LIB) -logc -g $(MACHDEP) -Wl,-Map,$(notdir $@).map

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:= $(CURDIR)/$(TARGET)
export VPATH	:= $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:= $(CURDIR)/$(BUILD)
export LD		:= $(CC)

export OFILES			:= common.o ftp.o loader.o vrt.o dol.o ftpii.o
export PRELOADER_OFILES	:= _$(TARGET).dol.o preloader.o dol.o
export INCLUDE			:= -I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)

.PHONY: $(BUILD) clean run

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

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
	@$(bin2o)

_$(TARGET).elf: $(OFILES)

DEPENDS = $(OFILES:.o=.d) $(PRELOADER_OFILES:.o=.d)
-include $(DEPENDS)

endif
