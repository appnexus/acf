test:main.c
	gcc -o example -std=gnu99 -I../src/ -I../build main.c -lcommon -L../build/lib

clean:
	rm test

check:
	LD_PRELOAD=../build/lib/libcommon.so ./test
