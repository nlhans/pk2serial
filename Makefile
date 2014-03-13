all:	build

build: main.c
	python setup.py build
	cp ./build/lib.linux-x86_64-2.7/pk2serial.so ./pk2serial.so
	python test.py
