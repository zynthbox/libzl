# This Python file uses the following encoding: utf-8
import ctypes
import os
from os.path import dirname, realpath
import sys

from PySide2.QtGui import QGuiApplication
from PySide2.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide2.QtCore import (
    Qt,
    QObject,
    Slot,
    Signal,
    Property
)

libzl = None


def init():
    global libzl

    try:

        libzl = ctypes.cdll.LoadLibrary(dirname(realpath(__file__)) + "/../build/libzl.so")
        libzl.initJuce()
    except Exception as e:
        libzl = None
        print(f"Can't initialise libzl library: {str(e)}")


class ClipAudioSource(object):
    def __init__(self, filepath: bytes):
        if libzl:
            libzl.startTimer.argtypes = [ctypes.c_int]

            libzl.ClipAudioSource_new.restype = ctypes.c_void_p
            libzl.ClipAudioSource_new.argtypes = [ctypes.c_char_p]

            libzl.ClipAudioSource_getDuration.restype = ctypes.c_float
            libzl.ClipAudioSource_getFileName.restype = ctypes.c_char_p
            libzl.ClipAudioSource_setStartPosition.argtypes = [ctypes.c_void_p, ctypes.c_float]
            libzl.ClipAudioSource_setLength.argtypes = [ctypes.c_void_p, ctypes.c_float]
            libzl.ClipAudioSource_setPitch.argtypes = [ctypes.c_void_p, ctypes.c_float]
            libzl.ClipAudioSource_setSpeedRatio.argtypes = [ctypes.c_void_p, ctypes.c_float]
            libzl.ClipAudioSource_play.argtypes = [ctypes.c_void_p]
            libzl.ClipAudioSource_stop.argtypes = [ctypes.c_void_p]

            self.obj = libzl.ClipAudioSource_new(filepath)
            self.filepath = filepath

    def play(self):
        if libzl:
            libzl.ClipAudioSource_play(self.obj)
            #startLoop(self.filepath)

    def stop(self):
        if libzl:
            libzl.ClipAudioSource_stop(self.obj)

    def get_duration(self):
        if libzl:
            return libzl.ClipAudioSource_getDuration(self.obj)

    def get_filename(self):
        if libzl:
            return libzl.ClipAudioSource_getFileName(self.obj)

    def set_start_position(self, start_position_in_seconds: float):
        if libzl:
            libzl.ClipAudioSource_setStartPosition(self.obj, start_position_in_seconds)

    def set_length(self, length_in_seconds: float):
        if libzl:
            libzl.ClipAudioSource_setLength(self.obj, length_in_seconds)

    def set_pitch(self, pitch_change: float):
        if libzl:
            libzl.ClipAudioSource_setPitch(self.obj, pitch_change)

    def set_speed_ratio(self, speed_ratio: float):
        if libzl:
            libzl.ClipAudioSource_setSpeedRatio(self.obj, speed_ratio)


class Bridge(QObject):
    def __init__(self, parent=None):
        super(Bridge, self).__init__(parent)
        path = "/zynthian/zynthian-my-data/capture/c4.wav"
        self.audioSource = ClipAudioSource(path.encode('utf-8'))

    @Slot(None)
    def play(self):
        self.audioSource.play()

    @Slot(None)
    def stop(self):
        self.audioSource.stop()



if __name__ == "__main__":
    init()

    app = QGuiApplication(sys.argv)
    engine = QQmlApplicationEngine()

    bridge = Bridge()
    engine.rootContext().setContextProperty("bridge", bridge)
    engine.load(os.fspath(dirname(realpath(__file__)) + "/playtest.qml"))

    if not engine.rootObjects():
        sys.exit(-1)
    sys.exit(app.exec_())
