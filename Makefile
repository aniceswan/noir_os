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

# User programs: the shell is the init process, USER_APPS are installed into /bin
USER_PROGRAMS = shell clock hello echo
USER_APPS     = clock hello echo
USER_BINARIES = $(addprefix $(OUTPUT_FOLDER)/,$(USER_PROGRAMS))
USER_ELFS     = $(addsuffix _elf,$(USER_BINARIES))

.PHONY: all build run clean kernel iso disk inserter user-shell user-programs insert-shell insert-programs prepare

run: all $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@qemu-system-i386 -s -S -rtc base=localtime -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk
all: build
build: iso

# Create a fresh 4 MiB EXT2 disk image
disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

# Build the ISO, recreate the disk and insert the user programs
prepare: clean build disk insert-programs
	@echo "Disk and ISO are ready. Run 'make run'."

$(OUTPUT_FOLDER)/$(DISK_NAME).bin:
	@qemu-img create -f raw $@ 4M

inserter:
	@$(CC) -Wno-builtin-declaration-mismatch -g -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c \
		$(SOURCE_FOLDER)/filesystem/ext2.c \
		$(SOURCE_FOLDER)/external/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

# --- User programs: link as ELF, then convert to a flat binary
# Temporary objects are named *crt0.o on purpose, user-linker.ld puts that file first
$(USER_ELFS): $(OUTPUT_FOLDER)/%_elf: $(USER_FOLDER)/%.c $(USER_FOLDER)/crt0.s $(USER_FOLDER)/user-linker.ld
	@$(ASM) $(AFLAGS) $(USER_FOLDER)/crt0.s -o $(OUTPUT_FOLDER)/$*_crt0.o
	@$(CC) $(CFLAGS) -fno-pie $< -o $(OUTPUT_FOLDER)/$*_main.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/$*_string.o
	@echo Linking $* object files and generate ELF32...
	@$(LIN) -T $(USER_FOLDER)/user-linker.ld -melf_i386 --no-warn-rwx-segments \
		$(OUTPUT_FOLDER)/$*_crt0.o $(OUTPUT_FOLDER)/$*_main.o $(OUTPUT_FOLDER)/$*_string.o -o $@
	@rm -f $(OUTPUT_FOLDER)/$*_crt0.o $(OUTPUT_FOLDER)/$*_main.o $(OUTPUT_FOLDER)/$*_string.o

$(USER_BINARIES): $(OUTPUT_FOLDER)/%: $(OUTPUT_FOLDER)/%_elf
	@$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents $< $@
	@size --target=binary $@

user-programs: $(USER_BINARIES)
user-shell: $(OUTPUT_FOLDER)/shell

# The shell (init process) lives in the root directory, other programs in /bin (default PATH)
insert-programs: inserter $(USER_BINARIES) $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@echo Inserting shell into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f shell 1 $(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); ./inserter -f ../$(USER_FOLDER)/readme.txt 1 $(DISK_NAME).bin
	@echo Inserting user programs into /bin...
	@cd $(OUTPUT_FOLDER); for app in $(USER_APPS); do ./inserter -f -p $$app /bin/$$app $(DISK_NAME).bin || exit 1; done
insert-shell: insert-programs

clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*/ $(OUTPUT_FOLDER)/kernel \
		$(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/iso $(OUTPUT_FOLDER)/inserter \
		$(USER_BINARIES) $(USER_ELFS)

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
