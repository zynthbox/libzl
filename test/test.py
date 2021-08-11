import ctypes
from os.path import dirname, realpath

libzl = None


try:
    libzl = ctypes.cdll.LoadLibrary(dirname(realpath(__file__)) + "/../build/libzl.so")
    libzl.init()
except Exception as e:
    libzl = None
    print(f"Can't initialise libzl library: {str(e)}")

print("Library Object :")
print(libzl)

print("\n\nPlay Wav Function :")
print(libzl.playWav)
