# This Python file uses the following encoding: utf-8
import ctypes
import os
from os.path import dirname, realpath
import sys

from PySide2.QtGui import QGuiApplication
from PySide2.QtQml import QQmlApplicationEngine

libzl = None


def init():
    global libzl

    try:

        libzl = ctypes.cdll.LoadLibrary(dirname(realpath(__file__)) + "/../build/libzl.so")
    except Exception as e:
        libzl = None
        print(f"Can't initialise libzl library: {str(e)}")


if __name__ == "__main__":
    print(dirname(realpath(__file__)))
    init()
    print("DDDD")
    app = QGuiApplication(sys.argv)
    engine = QQmlApplicationEngine()

    engine.load(os.fspath(dirname(realpath(__file__)) + "/waveform.qml"))

    if not engine.rootObjects():
        sys.exit(-1)
    sys.exit(app.exec_())
