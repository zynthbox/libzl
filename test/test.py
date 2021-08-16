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

libzl.ClipAudioSource_new.restype = ctypes.c_void_p
libzl.ClipAudioSource_new.argtypes = [ctypes.c_char_p]

clip = libzl.ClipAudioSource_new(b"/zynthian/zynthian-my-data/capture/je-kota-din_steinway.ogg")

libzl.ClipAudioSource_play(clip)
