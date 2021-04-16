Hardware Extensions
=================================
This guide will show you how to use the extension port to communicates with hardware extensions.

Understanding the extension port
++++++++++++++++++++++++
The extension port is located at the top cap of FLUX Delta. It has 10pins, including - 1 UARTs, 1 stepper motor control, 1 GND, 1 analog output, 3 digital input/output pins, 3.3V and 24V outputs. 

.. image:: toolhead/extension_port.png

**DESCRIPTION**

	**Tx / Rx** - Used for UART communication.

	**EN / STP / CT-REF / DIR** - Used for stepper motor control.

	**INPUT0** - Digital input.

	**OUTPUT 0-1** - Digital outputs.

	**PWM0** - Analog outputs.

	**GND** - Ground pin.

	**3.3V** - Can be used for powering MCUs, for example like `Arduino Pro Mini <https://www.arduino.cc/en/Main/ArduinoBoardProMini>`_. **The maximum current is 3A.**

	**24V** - Can be used for powering heater, drills or other power consuming components. **The maximum current is 3A.**



Developing firmware for hardware extensions
++++++++++++++++++++++++
A typical toolhead uses UART to communicate with its hosting machine , clone or download the example project to build your tool head.
::
	git clone https://github.com/flux3dp/selfdefined_toolhead_example

Communicating with hardware extensions
++++++++++++++++++++++++
::

	//Coming soon! You'll need to use SDK mode of the machine.
