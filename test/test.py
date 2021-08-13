import ctypes
from os.path import dirname, realpath

libzl = None

try:
    libzl = ctypes.cdll.LoadLibrary(dirname(realpath(__file__)) + "/../build/libzl.so")
except Exception as e:
    libzl = None
    print(f"Can't initialise libzl library: {str(e)}")

print("Library Object :")
print(libzl)

print("Testing Loop :")
clip = libzl.testLoop
