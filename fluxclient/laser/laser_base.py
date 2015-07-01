# !/usr/bin/env python3

from math import pi, sin, cos
import os


class LaserBase(object):
    """base class for all laser usage calss"""
    def __init__(self):
        self.laser_on = False
        self.focal_l = 11 + 3  # focal z coordinate
        self.rotation = 0.0

        # machine indicate how you pass gcode into machine
        # choose marlin if you are using printrun
        # self.machine = 'pi'
        if "FLUXHW" in os.environ:
            self.machine = os.environ["FLUXHW"]
        else:
            self.machine = 'pi'

        self.ratio = 1.

    def header(self, header):
        """
        header gcode for laser
        """
        gcode = []
        gcode.append(";Flux laser")
        gcode.append(";" + header)

        #  gcode.append("M666 X-1.95 Y-0.4 Z-2.1 R97.4 H241.2")
        gcode.append("M666 X-1.95 Y-0.4 Z-2.1 R97.4 H241.2")  # new

        gcode += self.turnOff()
        gcode.append("G28")
        gcode.append(";G29")

        gcode.append("G1 F5000 Z" + str(self.focal_l) + "")
        return gcode

    def turnOn(self):
        if self.laser_on:
            return []
        self.laser_on = True
        if self.machine == 'marlin':
            return ["G4 P10", "@X9L0", "G4 P1"]
        elif self.machine == 'pi':
            return ["G4 P10", "HL0", "G4 P1"]

    def turnOff(self):
        if not self.laser_on:
            return []
        self.laser_on = False
        if self.machine == 'marlin':
            return ["G4 P1", "@X9L255", "G4 P1"]
        elif self.machine == 'pi':
            return ["G4 P1", "HL255", "G4 P1"]

    def turnHalf(self):
        self.laser_on = False
        if self.machine == 'marlin':
            return ["G4 P1", "@X9L250", "G4 P1"]
        elif self.machine == 'pi':
            return ["G4 P1", "HL250", "G4 P1"]

    def moveTo(self, x, y, speed=600):
        """
            apply global rotation and scale
            move to position x,y
        """
        x2 = x * cos(self.rotation) - y * sin(self.rotation)
        y2 = x * sin(self.rotation) + y * cos(self.rotation)

        x = x2 * self.ratio
        y = y2 * self.ratio

        if speed == 'draw':
            speed = 200
        elif speed == 'move':
            speed = 600

        return ["G1 F" + str(speed) + " X" + str(x) + " Y" + str(y) + ";Draw to"]

    def drawTo(self, x, y, speed='draw'):
        """
            turn on, move to x, y and turn off the laser

            draw to position x,y with speed
        """
        gcode = []
        gcode += self.turnOn()
        gcode += self.moveTo(x, y, speed)
        gcode += self.turnOff()

        return gcode

    def to_image(self, buffer_data, img_width, img_height):
        """
        convert buffer_data(bytes) to a 2d array
        """
        int_data = list(buffer_data)
        assert len(int_data) == img_width * img_height, "data length != width * height, %d != %d * %d" % (len(int_data), img_width, img_height)
        image = [int_data[i * img_width: (i + 1) * img_width] for i in range(img_height)]

        return image
