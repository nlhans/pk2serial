pk2serial
=========

Python module for interfacing PICKIT2 UART mode with simple read() and write() functions.

Credits
=======

I've (tried to) implemented a Python wrapper around an existing pk2serial utility made by Tom Schouten. Please check the original code at: https://github.com/zwizwa/staapl/blob/master/tools/pk2serial.c

The wrapper integrates the utility into Python as a module, and allows opening/closing the port, setting the baudrate, power output, reset on connect, and standard read/write(string) functions.

Building
========

Run make from the command to build the Python module and run the test script. The test script opens the PICKIT2 port at 19200 baud, writes Hello World! to the PK2 and prints everything the PK2 receives.

The Python module is written for Python 2.7.

Usage
=====
See test.py for a simple usage. 

You can only change the baudrate, power output and reset-on-connect functionality before open(). You must close the port first before you change it.
Moreover, if you close the port the PICKIT2 resets itself and will take a brief moment before it is able to connect. Take note it may require a brief delay before connecting again.

As of now, I've only tested communication in ASCII format. No implementations or tests for binary communication has been done.

Notes
=====
This is my first attempt at writing a Python module to see how it's done and how hard can it be ;) 

I haven't used Python in a long time, so I'm also not completely familiar with the language (anymore). I'm open to any comments on the construction and code.
