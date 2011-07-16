export DEVKITARM := /d/ndsdev/devkitARM
export DEVKITPRO := /d/ndsdev/

VERSION=1.0

#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

export TARGET		:=	$(shell basename $(CURDIR))
export TOPDIR		:=	$(CURDIR)

ICON 		:= -b $(CURDIR)/logo.bmp "AnotherWorld $(VERSION);AlekMaul;http://www.portabledev.com"

.PHONY: arm7/$(TARGET).elf arm9/$(TARGET).elf

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: $(TARGET).nds

#---------------------------------------------------------------------------------
$(TARGET).nds	:	arm7/$(TARGET).elf arm9/$(TARGET).elf
	ndstool	-c $(TARGET).nds -7 arm7/$(TARGET).elf -9 arm9/$(TARGET).elf $(ICON)
	cp $(TARGET).nds $(TARGET)_fs.nds
#	dlditool r4tf $(TARGET)_fs.nds
	cp $(TARGET).nds ANOTHERWORLD/debug$(TARGET).nds
	rm "ANOTHERWORLD/debug$(TARGET)_nogba.nds"
  
#---------------------------------------------------------------------------------
arm7/$(TARGET).elf:
	$(MAKE) -C arm7
	
#---------------------------------------------------------------------------------
arm9/$(TARGET).elf:
	$(MAKE) -C arm9

#---------------------------------------------------------------------------------
clean:
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm7 clean
	rm -f $(TARGET).nds $(TARGET).arm7 $(TARGET).arm9
