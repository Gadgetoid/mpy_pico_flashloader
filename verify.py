import rp2
import machine

flash = rp2.Flash(start=0)

blocks = flash.ioctl(4, 0)

buf = bytearray(64)
flash.readblocks(blocks - 1, buf)
print(buf)


wbuf = bytes([66] * 64)

flash.writeblocks(blocks - 1, wbuf)
flash.readblocks(blocks - 1, buf)
print(buf)


#buf = open("blink.bin", "rb").read()
#flash.writeblocks(blocks, b"test")
#print("Done")
#machine.reset()