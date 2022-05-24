TUD (The Ugly Duckling), a simple serial port debugging tool for Linux.

Install

 $ make
 $ make install

Uninstall

 $ make uninstall

Debugging

Compiling with -g option

  $ gcc -o tud tud.c -g -lpthread
  $ gdb --args ./tud -o /dev/ttyUSB0 -w "hello, world" -r -v

Usage

 $ tud -h

Example

 Writing "hello, world" and waiting for data come from the target.

  $ tud -o /dev/ttyUSB0 -w "hello, world" -r -v -t 5 -c 4

 Using Hex mode

  $ tud -o /dev/ttyUSB0 -v -w "345" -x
  This would send two bytes (0x45, then 0x03) to the target device.

Usage: tud [OPTION]...
[OPTIONS]
  -b	setting baud rate, default 115200
  -h	display this message
  -o	open target serial port, eg. /dev/ttyUSB0
  -w	write target serial port
  -r	read target serial port
  -v	verbose mode
  -c	specify sending repeat counts
  -t	specify sending period
  -x	parse in hex mode
  -a	appending "\r\n" at the end
