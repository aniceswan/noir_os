# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc
OBJCOPY       = objcopy

# Directory
SOURCE_FOLDER = src
USER_FOLDER   = $(SOURCE_FOLDER)/user
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

# Source & object discovery (user programs are built separately)
ASM_SOURCES   = $(shell find $(SOURCE_FOLDER) -name '*.s' -not -path '$(USER_FOLDER)/*')
C_SOURCES     = $(filter-out $(SOURCE_FOLDER)/external/%,$(shell find $(SOURCE_FOLDER) -name '*.c' -not -path '$(USER_FOLDER)/*'))
ASM_OBJECTS   = $(patsubst $(SOURCE_FOLDER)/%.s,$(OUTPUT_FOLDER)/%.o,$(ASM_SOURCES))
C_OBJECTS     = $(patsubst $(SOURCE_FOLDER)/%.c,$(OUTPUT_FOLDER)/%.o,$(C_SOURCES))
OBJECTS       = $(ASM_OBJECTS) $(C_OBJECTS)

.PHONY: all build run clean kernel iso disk inserter user-shell insert-shell prepare

run: all $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@qemu-system-i386 -s -S -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk
all: build
build: iso

# Create a fresh 4 MiB EXT2 disk image
disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

# Build the ISO, recreate the disk and insert user shell
prepare: clean build disk insert-shell
	@echo "Disk and ISO are ready. Run 'make run'."

$(OUTPUT_FOLDER)/$(DISK_NAME).bin:
	@qemu-img create -f raw $@ 4M

inserter:
	@$(CC) -Wno-builtin-declaration-mismatch -g -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c \
		$(SOURCE_FOLDER)/filesystem/ext2.c \
		$(SOURCE_FOLDER)/external/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

# --- User shell: link as ELF, then convert to a flat binary
$(OUTPUT_FOLDER)/shell_elf: $(USER_FOLDER)/shell.c $(USER_FOLDER)/crt0.s
	@$(ASM) $(AFLAGS) $(USER_FOLDER)/crt0.s -o crt0.o
	@$(CC) $(CFLAGS) -fno-pie $< -o program.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o string.o
	@echo Linking shell object files and generate ELF32...
	@$(LIN) -T $(USER_FOLDER)/user-linker.ld -melf_i386 --no-warn-rwx-segments \
		crt0.o program.o string.o -o $@
	@rm -f crt0.o program.o string.o

$(OUTPUT_FOLDER)/shell: $(OUTPUT_FOLDER)/shell_elf
	@$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents $< $@
	@size --target=binary $@

user-shell: $(OUTPUT_FOLDER)/shell

insert-shell: inserter $(OUTPUT_FOLDER)/shell $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@echo Inserting shell into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f shell 1 $(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); ./inserter -f ../$(USER_FOLDER)/readme.txt 1 $(DISK_NAME).bin

clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*/ $(OUTPUT_FOLDER)/kernel \
		$(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/iso $(OUTPUT_FOLDER)/inserter \
		$(OUTPUT_FOLDER)/shell $(OUTPUT_FOLDER)/shell_elf

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
