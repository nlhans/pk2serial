import pk2serial

pk2serial.power(0) # enable power if using loopback, enter a voltage
pk2serial.resetOnConnect(1)

if (pk2serial.open(19200) == 1):
	print("Connected succesfully to PK2 @ 19k2 baud")
	
	pk2serial.write("Hello World!\n");

	while pk2serial.isOpen():
		read = pk2serial.read()

		if read != False:
			print(read)

	pk2serial.close()
	# Note: don't open pk2serial quickly agian, as the device must reenumerate first.
	# Add a delay.
else:
	print("Failed to connect")
