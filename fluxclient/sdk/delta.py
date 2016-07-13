# -*- coding: utf-8 -*-
"""

.. moduleauthor:: Yen Feng <yen@flux3dp.com>

"""
from io import BytesIO
import logging
from uuid import UUID
import socket
from time import sleep, time
import threading

from msgpack import packb, unpackb, Unpacker
from PIL import Image

from fluxclient.hw_profile import HW_PROFILE
from fluxclient.encryptor import KeyObject
from fluxclient.upnp.task import UpnpTask
from fluxclient.commands.misc import get_or_create_default_key
from fluxclient.robot import FluxRobot
from fluxclient.sdk import *

logger = logging.getLogger(__name__)


def type_check(instance, type_candidates, err_msg=''):
    if isinstance(instance, type_candidates):
        return
    else:
        raise TypeError('{} type: {} is not supported'.format(err_msg, type(instance)))


class Delta(object):
    """
      Delta is an instance that presents the device in SDK mode, which allows users to control motions of the machine freely.
    """
    def __init__(self, wrapped_socket, exit_callback=None, blocking=True):
        """
            Creates a Delta ( SDK task ) instance

            :param str wrapped_socket: Wrapped sockets
            :param callback exit_callback: Callback when exited
            :param bool blocking: Blocking means python will only send commands when prior commands are finished, otherwise the device will buffer commands.
        """
        super(Delta, self).__init__()

        self._command_index = -1
        self.tool_head_pos = [0, 0, HW_PROFILE["model-1"]["height"]]
        self.motor_pos = {"e0": 0.0, "e1": 0.0, "e2": 0.0}
        self.laser_status = {"L": False, "R": False}
        self.motor_status = {"e0": True, "e1": True, "e2": False}  # [("e0", "E"), ("e1", "S"), ("e2", "U")]
        self.loose_flag = False
        self.lock = threading.Lock()
        self.command_output = []  # record all the output return from command

        self.control_sock = wrapped_socket
        self.exit_callback = exit_callback
        self.blocking_flag = blocking

        self.head_status = [b'', -2, 0, {}]
        self.open_udp_sock()

        self.connected = True

    def __del__(self):
        if self.connected:
            logger.debug('wait for commands to finished')
            while self.atomic_status()[0] - 1 != self._command_index or self.atomic_status()[1] != 0:
                # print(self.atomic_status())
                pass
            logger.debug('wait for udp socket to close')
            self.killthread = True
            self.queue_checker.join()
            sleep(1)

            self._command_index += 1
            self.send_command([CMD_QUIT])
            if self.exit_callback:
                self.exit_callback()
            sleep(1)
            self.connected = False

    @property
    def head_type(self):
        return self.head_status[3].get(b'module', b'N/A')

    @classmethod
    def connect_delta(cls, target=None, ip=None, client_key=None, password=None, kick=False, blocking=True):
        """
            Creates a Delta instance

            :param str uuid: Device's UUID, optional
            :param str ip: Device's IP, optional
            :param str password: Device's password
            :param bool kick: Force to kick other users
            :param bool blocking: Blocking means python will only send commands when prior commands are finished.
        """
        if client_key:
            if type(client_key) == str:
                client_key = get_or_create_default_key(client_key)
            else:
                type_check(client_key, (str, KeyObject), 'client_key')
        else:
            client_key = get_or_create_default_key()

        if target:
            type_check(target, str, 'target')
            target = UUID(target)

        if ip:
            type_check(ip, str, 'ip')

        if password:
            type_check(password, str, 'password')

        options = {'uuid': UUID(int=0)}
        for i, name in [(target, 'uuid'), (ip, 'ipaddr'), (client_key, 'client_key')]:
            if i:
                options[name] = i
        if password:
            options['backend_options'] = {'password': password}

        upnp_task = UpnpTask(**options)

        if not upnp_task.authorized:
            if password:
                upnp_task.authorize_with_password(password)
                if upnp_task.authorized:
                    upnp_task.add_trust('sdk key', client_key.public_key_pem.decode())
                    logger.warning('[Warning]: adding new key into flux delta')
        if upnp_task.authorized:
            robot = FluxRobot((upnp_task.ipaddr, 23811), client_key)

            st_id = robot.report_play()["st_id"]
            if st_id > 0:
                if st_id not in [64, 66, 128, 130]:
                    robot.abort_play()

                while robot.report_play()["st_id"] in [66, 130]:
                    sleep(0.5)

                robot.quit_play()
            elif st_id < 0:
                robot.kick()

            while robot.report_play()["st_id"] != 0:
                sleep(0.5)
            m_delta = robot.icontrol()  # retuen a Delta object
            m_delta.blocking_flag = blocking
            return m_delta
        else:
            raise RuntimeError("cannot connect to flux delta")

    def open_udp_sock(self):
        local_ipaddr = self.control_sock.getsockname()
        self.send_command([CMD_SYNC, local_ipaddr[0], 22111, b""])
        # init udp socket: for state
        self.status = (1, 0)  # index, queue_left
        self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_sock.bind((socket.gethostbyname(socket.gethostname()), 22111))
        buf = self.udp_sock.recv(4096)
        self.killthread = False
        self.queue_checker = threading.Thread(target=self.delta_status)
        self.queue_checker.start()

    def atomic_status(self, new_staus=None):
        """
        Returns the index, queue_left of commands buffer
        """
        self.lock.acquire()

        if new_staus:
            self.status = new_staus
        else:
            new_staus = self.status
        self.lock.release()
        return new_staus

    def get_result(self, index, wait=True):
        """
        Acquires the result of certain index from Delta
        """
        self.lock.acquire()
        if len(self.command_output) < index:
            raise SDKFatalError(self, 'Fatal Error: command index error(index:{}, total:{})'.format(index, len(self.command_output)))
        else:
            if callable(self.command_output[index]):
                if wait:
                    self.lock.release()
                    while callable(self.command_output[index]):
                        # busy waiting for return value
                        pass
                    self.lock.acquire()
                    ret = self.command_output[index]
                else:
                    raise SDKFatalError(self, 'Not ready(index:{})'.format(index))
            elif self.command_output[index] is False:
                if wait:
                    self.lock.release()
                    while self.command_output[index] is False:
                        # print(index, self.command_output)
                        # sleep(0.5)
                        # busy waiting for return value
                        pass
                    self.lock.acquire()
                    ret = self.command_output[index]
                else:
                    raise SDKFatalError(self, 'Not ready(index:{})'.format(index))
            else:
                ret = self.command_output[index]
        self.lock.release()
        return ret

    def delta_status(self):
        """
        Loop thread function that collects and updates delta's status and command's output
        """
        recv_until = -1
        # udp_index = 0
        unpacker = Unpacker()
        while True:
            if self.killthread:
                self.udp_sock.close()
                return
            else:
                buf = self.udp_sock.recv(4096)
                payload = unpackb(buf)

                if payload[0] == 0:
                    # print('payload', payload)
                    if payload[2] - 1 - payload[3] > recv_until:  # finish_index = next_index - 1 - command_in_queue
                        # print('able range', recv_until + 1, payload[2] - 1 - payload[3] + 1)
                        for i in range(recv_until + 1, payload[2] - 1 - payload[3] + 1):
                            if self.command_output[i]:
                                ret = self.control_sock.recv(4096)
                                unpacker.feed(ret)
                                ret = unpacker.__next__()
                                self.lock.acquire()
                                self.command_output[i] = self.command_output[i](ret)
                                self.lock.release()
                            elif self.command_output[i] is False:
                                self.lock.acquire()
                                self.command_output[i] = None
                                self.lock.release()
                            else:
                                print('         --->>>no way!!', self.command_output[i], self.command_output)

                        recv_until = payload[2] - 1 - payload[3]

                    self.atomic_status((payload[2], payload[3]))
                elif payload[0] == 1:
                    self.lock.acquire()
                    self.head_status = payload[1:]
                    self.head_error = payload[3]
                    if self.head_error > 0:
                        # TODO
                        self.send_command([CMD_CLHE])
                    self.lock.release()

    def send_command(self, command, recv_callback=False):
        self._command_index += 1
        self.control_sock.send(packb(tuple([self._command_index] + command)))

        if recv_callback:
            self.command_output.append(recv_callback)
        elif recv_callback is False:
            self.command_output.append(False)
        else:
            print('no way2!')
        # print('send', self.command_output)
        # print('index', self._command_index)
        return self._command_index

    def get_position(self):
        """
        Gets the current position of toolhead

        :return: coordinate of each tower
        :rtype: (float, float, float)

        >>> f.get_position()
        (0, 0, 280)
        """
        return tuple(self.tool_head_pos)

    def home(self):
        """
        Sets each axis back to home.

        :return: the coordinate where the toolhead originally is
        :rtype: (float, float, float)

        >>> f.home()
        (20.0, 20.0, 20.0)
        >>> f.get_position()
        (0, 0, 280)
        """
        def post_process(ret):
            if ret[0] != CMD_G028 or ret[1] != 0:
                print('ret:', ret)
                raise RuntimeError('command retrun error')
            else:
                return tuple(ret[2])
        self.loose_flag = False
        command_index = self.send_command([CMD_G028], recv_callback=post_process)
        ret = self.get_result(command_index, wait=True)
        if self.blocking_flag:
            return command_index, ret
        else:
            return command_index, None

    def move(self, x=None, y=None, z=None, speed=None, relative=False, **kargs):
        """
        Move the toolhead to (x, y, z)

        :param float x: x coordinate, optional
        :param float y: y coordinate, optional
        :param float z: z coordinate, optional
        :param bool relative: moving relatively or not, default: False

        :param float e0: length mortor e0 move , optional
        :param float e1: length mortor e0 move , optional
        :param float e2: length mortor e0 move , optional


        :return: position after moving command
        :rtype: (float, float, float)

        :raises ValueError: if trying to the exceed the limitation of machine

        * basic usage

        >>> f.get_position()
        (0.0, 0.0, 280.0)
        >>> f.move(50, 40, 30)
        (50.0, 40.0, 30.0)
        >>> f.move(x=80)  # only sets the coordinate you want
        (80.0, 40.0, 30.0)
        >>> f.move(x=-10, relative=True)  # using relative flag
        (70.0, 40.0, 30.0)
        >>> f.move(z=-100)
        ValueError: Invalid coordinate

        * control motor during moving

        >>> f.move(50, 40, 30, e0=10)
        (50.0, 40.0, 30.0)

        """
        tmp_pos = self.tool_head_pos[:]
        for name, v, index in [("x", x, 0), ("y", y, 1), ("z", z, 2)]:
            if isinstance(v, (int, float)):
                v = float(v)
                if relative:
                    tmp_pos[index] += v
                else:
                    tmp_pos[index] = float(v)
            elif v is None:
                pass
            else:
                raise SDKFatalError(self, "unsupported type({1}) for {0} coordinate".format(name, type(v)))
                break

        if isinstance(speed, (int, float)):
            speed = int(speed)

        if tmp_pos[0] ** 2 + tmp_pos[1] ** 2 > HW_PROFILE["model-1"]["radius"] ** 2 or tmp_pos[2] > HW_PROFILE["model-1"]["height"] or tmp_pos[2] < 0:
            raise SDKFatalError(self, "Invalid coordinate")
        else:
            self.tool_head_pos = tmp_pos

        if self.loose_flag:
            raise SDKFatalError(self, 'motor need to home() before moving')
        else:
            tmp_d = {'X': self.tool_head_pos[0], 'Y': self.tool_head_pos[1], 'Z': self.tool_head_pos[2]}
            if speed:
                tmp_d['f'] = int(speed)

            command_index = self.send_command([CMD_G001, tmp_d], recv_callback=False)
            if self.blocking_flag:
                return command_index, self.get_result(command_index, wait=True)
            else:
                return command_index, None

        # ({i:F, f:X, f:Y, f:Z, f:E1, f:E2 , f:E3})

        return tuple(self.tool_head_pos)

    def get_position_motor(self, motor):
        """
        Gets the current position of specified motor

        :param str motor: name of the motor, should be one of the ``"e0", "e1", "e2"``
        :return: the current position of specified motor
        :rtype: float
        :raises ValueError: raise error if using undifine motor name

        >>> f.get_position_motor("e0")
        12.34

        """
        if motor in self.motor_pos:
            return self.motor_pos[motor]
        else:
            raise SDKFatalError(self, "Invalid motor name {}".format(motor))

    def move_motor(self, e0=None, e1=None, e2=None):
        """
        Moves stepper motors

        :param float e0: length to move, optional
        :param float e1: length to move, optional
        :param float e2: length to move, optional

        >>> f.move_motor(e0=10)
        >>> f.move_motor(e0=10, e1=10)

        """
        self.move(e0=e0, e1=e1, e2=e2)

    def lock_motor(self):
        """
        Locks all motor.

        >>> f.lock_motor('XYZ')

        """
        command_index = self.send_command([CMD_M017], recv_callback=False)
        if self.blocking_flag:
            return command_index, self.get_result(command_index, wait=True)
        else:
            return command_index, None

    def release_motor(self):
        """
        Releases all motor.

        .. note::

            If you ever released stepper motors, You have to :meth:`home` before calling :meth:`move` them.

        >>> f.release_motor()

        """
        # if motor == 'XYZ':
        self.loose_flag = True
        command_index = self.send_command([CMD_M084], recv_callback=False)
        if self.blocking_flag:
            return command_index, self.get_result(command_index, wait=True)
        else:
            return command_index, None

    def get_position_laser(self, laser):
        """
        Finds out whether the laser is on

        :param str laser: Which laser, should be ``"L"`` or ``"R"``
        :raises ValueError: if the laser parameter is not ``"L"`` or ``"R"``

        >>> f.get_position_laser('L')
        True

        """

        if laser == "L" or laser == "R":
            return self.laser_status[laser]
        else:
            if isinstance(laser, str):
                raise SDKFatalError(self, "Invalid laser name: {}".format(laser))
            else:
                raise SDKFatalError(self, "Invalid argument type")

    def turn_laser(self, laser, on):
        """
        Controls the scanning laser

        :param str laser: Which laser, should be ``"L"`` or ``"R"``
        :param bool on: turn *on* or *off* the laser
        :raises ValueError: if the laser parameter is not ``"L"`` or ``"R"``

        >>> f.turn_laser('L', True)  # Turn on left laser
        True
        >>> f.turn_laser('R', False)  # Turn off right laser
        False

        """

        if laser == "L" or laser == "R":
            self.laser_status[laser] = bool(on)

            flag = 0
            if self.laser_status['L']:
                flag |= 1 << 0
            if self.laser_status['R']:
                flag |= 1 << 1

            command_index = self.send_command([CMD_SLSR, flag], recv_callback=False)
            if self.blocking_flag:
                return command_index, self.get_result(command_index, wait=True)
            else:
                return command_index, None

        else:
            if isinstance(laser, str):
                raise SDKFatalError(self, "Invalid laser name: {}".format(laser))
            else:
                raise SDKFatalError(self, "Invalid argument type")

        return self.laser_status[laser]

    def get_image(self):
        """
        Takes a picture with Delta's built in camera

        :return: a image object
        :rtype: PIL.Image.Image

        """
        images = self.robot.oneshot()
        image_buf = images[0][1]

        return Image.open(BytesIO(image_buf))

    def serial_write(self, buf):
        """
        [TODO]: support this in the future
        """
        pass

    def serial_read(self, buf_size):
        """
        [TODO]: support this in the future
        """
        pass

    def disable_motor(self, motor):
        """
        Enables motor control

        :param str motor: Which motor to disable, should be one of ``"XYZ"``, ``"e0"``, ``"e1"``, ``"e2"``
        :raises TypeError: motor is not str
        :raises ValueError: if the motor's name is not one of ``"XYZ"``, ``"e0"``, ``"e1"``, ``"e2"``

        """
        if motor in ["XYZ", "e0", "e1", "e2"]:
            self.motor_status[motor] = False
        else:
            if isinstance(motor, str):
                raise SDKFatalError(self, "Wrong motor name")
            else:
                raise SDKFatalError(self, "Wrong motor type: {}".format(type(motor)))

    def enable_motor(self, motor):
        """
        Disables a motor

        :param str motor: Which motor to enable, should be one of ``"XYZ"``, ``"e0"``, ``"e1"``, ``"e2"``
        :raises TypeError: motor is not str
        :raises ValueError: if the motor's name is not one of ``"XYZ"``, ``"e0"``, ``"e1"``, ``"e2"``

        """
        if motor in ["XYZ", "e0", "e1", "e2"]:
            self.motor_status[motor] = True
        else:
            if isinstance(motor, str):
                raise SDKFatalError(self, "Wrong motor name")
            else:
                raise SDKFatalError(self, "Wrong motor type: {}".format(type(motor)))

    def get_head_profile(self):
        """
        Gets the basic toolhead info(immutable data).

        :return: dict consist of toolhead's basic information
        :rtype: dict

        >>> f.get_head_profile()  # no tool head connected
        {"module": "N/A"}

        >>> f.get_head_profile()  # print head
        {"version": "1.0.8", "module": "EXTRUDER", "id": "203236325346430100240001", "vendor": "FLUX .inc"}

        >>> f.get_head_profile()  # laser head
        {"version": "1.0.3", "module": "LASER", "id":"203236325346430100260004", "vendor": "FLUX .inc"}


        .. note::

            For current toolhead status use :meth:`get_head_status`

        """
        def post_process(ret):
            return {i.decode(): ret[1][i].decode() for i in ret[1]}

        self.send_command([CMD_THPF])
        command_index = self.send_command([CMD_THPF], recv_callback=post_process)
        ret = self.get_result(command_index, wait=True)
        if self.blocking_flag:
            return command_index, ret
        else:
            return command_index, None

    def get_head_status(self):
        """
        Gets the current toolhead status(mutable data).

        :return: dict consist of toolhead's currrent information
        :rtype: dict

        >>> flux.get_head_status()
        {"real temp": [24.8], "target fan": [0.0], "target temp":[nan]}

        .. note::

            For basic toolhead information use :meth:`get_head_profile`

        """
        ret = {}
        for i in self.head_status[3]:
            if type(self.head_status[3][i]) == bytes:
                ret[i.decode()] = self.head_status[3][i].decode()
            else:
                ret[i.decode()] = self.head_status[3][i]
        if self.head_type == b"EXTRUDER":
            ret["real temp"] = ret.pop('rt')
            ret["target temp"] = ret.pop('tt')
            ret["target fan"] = ret.pop('tf')
        return ret

    def set_temp(self, temp, index=0):
        """
        Sets the temperature of print head

        :param number power: The power of print head, should be within [0.0, 200.0]
        :raises TypeError: if temp is not a number
        :raises ValueError: if the temp is not in [0.0, 200.0]
        :raises RuntimeError: if using wrong type of toolhead

        >>> f.set_temp(200)
        """
        if self.head_type == b"EXTRUDER":
            if isinstance(temp, (int, float)):
                if temp <= 200.0 and temp >= 0.0:
                    self.send_command([CMD_M104, index, int(temp)])
                else:
                    raise SDKFatalError(self, "Invalid temperature")
            else:
                raise SDKFatalError(self, "unsupported temp type: {}".format(type(temp)))
        else:
            raise SDKFatalError(self, 'Head error: {}, require head: "EXTRUDER"'.format(self.head_type))

    def set_fan(self, speed, toolhead_index=0):
        """
        Sets the speed of toolhead's fan

        :param number speed: The power of laser head, should be within [0.0, 1.0]
        :raises TypeError: if speed is not a number
        :raises ValueError: if the power is not in [0.0, 1.0]
        :raises RuntimeError: if using wrong type of toolhead

        >>> f.set_fan(0.5)
        """
        if self.head_type == b"EXTRUDER":
            if isinstance(speed, (int, float)):
                speed = float(speed)
                if speed <= 1.0 and speed >= 0.0:
                    print('speed', speed)
                    self.send_command([CMD_M106, toolhead_index, speed])
                else:
                    raise SDKFatalError(self, "Invalid fan speed:{}, should be in [0.0, 1.0]".format(speed))
            else:
                raise SDKFatalError(self, "unsupported speed type: {}".format(type(speed)))
        else:
            raise SDKFatalError(self, 'Head error: {}, require head: "EXTRUDER"'.format(self.head_type.decode()))
            # raise RuntimeError()

    def set_power(self, power):
        """
        Sets the power of laser toolhead

        :param number power: The power of laser head, should be within [0.0, 1.0]
        :raises TypeError: if power is not a number
        :raises ValueError: if the power is not in [0.0, 1.0]
        :raises RuntimeError: if using wrong type of toolhead

        >>> f.set_power(0.5)
        """
        if self.head_type == b"LASER":  # TODO
            if isinstance(power, (int, float)):
                if power <= 1.0 and power >= 0.0:
                    self.send_command([CMD_HLSR, power])
                else:
                    raise SDKFatalError(self, "Invalid laser power, should be within [0.0, 1.0]")
            else:
                raise SDKFatalError(self, "unsupported power type: {}".format(type(power)))
        else:
            ret = self.set_head("LASER")
            if not ret:
                raise SDKFatalError(self, 'Head error: {}, require head: "LASER"'.format(self.head_type))

    def set_head(self, head_type):
        if head_type in ['EXTRUDER', 'LASER', 'N/A']:
            self.send_command([CMD_REQH, head_type], False)
            self.send_command([CMD_BSTH])
            time_s = time()
            while True:  # wait for head to be ready
                if self.head_status[1] == 0:
                    return True
                elif time() - time_s > 5:  # not ready for too long
                    return False
                else:
                    sleep(0.5)
                    continue
        else:
            raise SDKFatalError(self, "Head error: {}".format(self.head_type))

    def close(self):
        self.__del__()


class SDKFatalError(Exception):
    def __init__(self, delta, err_msg):
        delta.close()
        self.err_msg = err_msg

    def __repr__(self):
        return self.err_msg

    def __str__(self):
        return self.err_msg


class SDKRunningError(Exception):
    def __init__(self, delta, err_msg):
        self.err_msg = err_msg

    def __repr__(self):
        return self.err_msg

    def __str__(self):
        return self.err_msg

if __name__ == '__main__':
    f = Delta.connect_delta(ip='192.168.18.114', password='flux', kick=True)

    # f.get_head_profile()

    # print('----------------------------------------------------------------------->', f.home())
    print(f.set_head('LASER'))
    # print(f.set_head('EXTRUDER'))
    print(f.get_head_status())
    # print('slepping')
    # sleep(10)
    # print(f.set_fan(-1))
    print(f.set_fan(1))
    print('slepping')
    sleep(10)
    f.close()
