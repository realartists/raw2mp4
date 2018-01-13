all: generate raw2mp4

generate: generate.c
	clang -framework CoreServices -framework ImageIO -framework CoreFoundation -framework CoreGraphics -o generate generate.c
	
raw2mp4: raw2mp4.c
	clang -Os -I../build/include -L../build/lib -lx264 -llsmash -o raw2mp4 raw2mp4.c
	
raw2mp4.bc: raw2mp4.c
	emcc -I../build_js/include -o raw2mp4.bc raw2mp4.c

raw2mp4.js: raw2mp4.bc
	emcc raw2mp4.bc ../build_js/lib/libx264.dylib ../build_js/lib/liblsmash.so -o raw2mp4.js -s TOTAL_MEMORY=67108864 -Os --memory-init-file 0

clean:
	rm -f generate raw2mp4 raw2mp4.js raw2mp4.bc

