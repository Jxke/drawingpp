from pyaxidraw import axidraw
import time


def main():
    ad = axidraw.AxiDraw()
    ad.interactive()
    ad.options.units = 2  # 2 = centimeters
    # Explicitly set serial port seen on this machine; adjust if your device uses another.
    ad.options.port = "/dev/cu.usbmodem141301"

    if not ad.connect():
        return

    ad.options.pen_pos_up = 100
    ad.update()
    ad.penup()
    time.sleep(0.333)

    ad.moveto(50, 50)

    ad.options.pen_pos_down = 80
    ad.update()
    ad.pendown()
    ad.lineto(50, 100)

    ad.options.pen_pos_down = 60
    ad.update()
    ad.pendown()
    ad.lineto(100, 50)

    ad.options.pen_pos_down = 20
    ad.update()
    ad.pendown()
    ad.lineto(50, 50)

    ad.penup()
    ad.moveto(0, 0)
    ad.disconnect()


if __name__ == "__main__":
    main()
