# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386

# Source & object discovery
ASM_SOURCES   = $(shell find $(SOURCE_FOLDER) -name '*.s')
C_SOURCES     = $(shell find $(SOURCE_FOLDER) -name '*.c')
ASM_OBJECTS   = $(patsubst $(SOURCE_FOLDER)/%.s,$(OUTPUT_FOLDER)/%.o,$(ASM_SOURCES))
C_OBJECTS     = $(patsubst $(SOURCE_FOLDER)/%.c,$(OUTPUT_FOLDER)/%.o,$(C_SOURCES))
OBJECTS       = $(ASM_OBJECTS) $(C_OBJECTS)

.PHONY: all build run clean kernel iso disk

disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

$(OUTPUT_FOLDER)/$(DISK_NAME).bin:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

run: all $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@qemu-system-i386 -s -S -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

all: build
build: iso
clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/kernel $(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/iso


kernel: $(OBJECTS)
	@$(LIN) $(LFLAGS) $(OBJECTS) -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...

$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.s
	@mkdir -p $(dir $@)
	@$(ASM) $(AFLAGS) $< -o $@

$(OUTPUT_FOLDER)/%.o: $(SOURCE_FOLDER)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $< -o $@

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage -R                   \
		-b boot/grub/grub1         \
		-no-emul-boot              \
		-boot-load-size 4          \
		-A os                      \
		-input-charset utf8        \
		-quiet                     \
		-boot-info-table           \
		-o $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		$(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/
