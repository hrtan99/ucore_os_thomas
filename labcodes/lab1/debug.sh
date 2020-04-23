qemu-system-i386 -S -s -monitor stdio -hda ./bin/ucore.img
sleep 2
gdb -tui -x ./tools/gdbinit
