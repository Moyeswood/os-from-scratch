all: tetris.img

tetris.img: tetris.asm
	nasm -f bin $< -o $@
	@ls -l $@

# 带声音运行 (PC 扬声器经 PulseAudio/PipeWire 输出; 没声音就换成 pipewire 或 alsa)
run: tetris.img
	qemu-system-i386 -drive format=raw,file=tetris.img \
	  -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

# 不带声音
run-quiet: tetris.img
	qemu-system-i386 -drive format=raw,file=tetris.img

# 带 GDB 调试: 另开终端 gdb -ex 'target remote :1234' -ex 'set architecture i8086'
debug: tetris.img
	qemu-system-i386 -drive format=raw,file=tetris.img -s -S

clean:
	rm -f tetris.img
