#!/usr/bin/env python3
"""Generates modern, razor-sharp 22x22 outline icons (Lucide/Feather inspired)
for Axion Cyber Multi-Tool. Outputs src/icon_assets.cpp and icons_data.js.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CPP_OUT = ROOT / "src" / "icon_assets.cpp"
JS_OUT = ROOT / "icons_data.js"

class Canvas22:
    def __init__(self):
        self.grid = [[0 for _ in range(22)] for _ in range(22)]

    def set(self, x, y, v=1):
        if 0 <= x < 22 and 0 <= y < 22:
            self.grid[y][x] = 1 if v else 0

    def line(self, x0, y0, x1, y1):
        dx = abs(x1 - x0)
        dy = abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self.set(x0, y0)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def rect(self, x, y, w, h):
        for i in range(x, x + w):
            self.set(i, y)
            self.set(i, y + h - 1)
        for j in range(y, y + h):
            self.set(x, j)
            self.set(x + w - 1, j)

    def fill_rect(self, x, y, w, h, v=1):
        for i in range(x, x + w):
            for j in range(y, y + h):
                self.set(i, j, v)

    def circle(self, cx, cy, r):
        x = r
        y = 0
        err = 0
        while x >= y:
            self.set(cx + x, cy + y)
            self.set(cx + y, cy + x)
            self.set(cx - y, cy + x)
            self.set(cx - x, cy + y)
            self.set(cx - x, cy - y)
            self.set(cx - y, cy - x)
            self.set(cx + y, cy - x)
            self.set(cx + x, cy - y)
            y += 1
            err += 1 + 2*y
            if 2*(err-x) + 1 > 0:
                x -= 1
                err += 1 - 2*x

    def to_xbm_bytes(self):
        # 22x22 XBM: 3 bytes per row (24 bits, last 2 bits zero padding), 66 bytes total.
        # Standard XBM: bit 1 = foreground, bit 0 = background.
        res = []
        for y in range(22):
            for b in range(3):
                byte_val = 0
                for bit in range(8):
                    x = b * 8 + bit
                    if x < 22 and self.grid[y][x]:
                        byte_val |= (1 << bit)
                res.append(byte_val)
        return res

def create_icons():
    icons = {}

    # 0: ICON_ATTACK (Lightning Bolt - Lucide Zap)
    c = Canvas22()
    c.line(12, 1, 6, 11)
    c.line(6, 11, 11, 11)
    c.line(11, 11, 9, 20)
    c.line(9, 20, 16, 9)
    c.line(16, 9, 11, 9)
    c.line(11, 9, 12, 1)
    icons[0] = ("Attack / Lightning", c)

    # 1: ICON_BEACON_SNIFF (Antenna tower / radar broadcast)
    c = Canvas22()
    c.line(11, 6, 11, 20)
    c.line(7, 20, 15, 20)
    c.circle(11, 5, 2)
    # Radiating arcs
    c.set(7, 3); c.set(6, 4); c.set(6, 6); c.set(7, 7)
    c.set(15, 3); c.set(16, 4); c.set(16, 6); c.set(15, 7)
    c.set(4, 1); c.set(3, 3); c.set(3, 7); c.set(4, 9)
    c.set(18, 1); c.set(19, 3); c.set(19, 7); c.set(18, 9)
    icons[1] = ("Beacon Sniff", c)

    # 2: ICON_BLUETOOTH (Modern Bluetooth Rune)
    c = Canvas22()
    c.line(10, 2, 10, 19)
    c.line(10, 2, 16, 7)
    c.line(16, 7, 5, 14)
    c.line(5, 7, 16, 14)
    c.line(16, 14, 10, 19)
    icons[2] = ("Bluetooth", c)

    # 3: ICON_BLUETOOTH_SNIFF (Bluetooth with scan waves)
    c = Canvas22()
    c.line(8, 2, 8, 19)
    c.line(8, 2, 14, 7)
    c.line(14, 7, 3, 14)
    c.line(3, 7, 14, 14)
    c.line(14, 14, 8, 19)
    c.set(17, 7); c.set(18, 9); c.set(18, 12); c.set(17, 14)
    c.set(20, 5); c.set(21, 8); c.set(21, 13); c.set(20, 16)
    icons[3] = ("Bluetooth Sniff", c)

    # 4: ICON_DEAUTH_SNIFF (Wi-Fi with break slash)
    c = Canvas22()
    # Wi-Fi arcs
    c.line(5, 6, 16, 6)
    c.line(7, 10, 14, 10)
    c.line(9, 14, 12, 14)
    c.fill_rect(10, 17, 2, 2)
    # Strikethrough slash
    c.line(3, 19, 18, 3)
    icons[4] = ("Deauth Sniff", c)

    # 5: ICON_DRAW (Document Sheet / Notepad)
    c = Canvas22()
    c.line(4, 2, 13, 2)
    c.line(13, 2, 17, 6)
    c.line(17, 6, 17, 20)
    c.line(17, 20, 4, 20)
    c.line(4, 20, 4, 2)
    c.line(13, 2, 13, 6)
    c.line(13, 6, 17, 6)
    c.line(7, 9, 14, 9)
    c.line(7, 12, 14, 12)
    c.line(7, 15, 11, 15)
    icons[5] = ("Notepad / Draw", c)

    # 6: ICON_PACKET_MONITOR (Oscilloscope waveform)
    c = Canvas22()
    c.rect(2, 3, 18, 16)
    c.line(3, 11, 6, 11)
    c.line(6, 11, 8, 6)
    c.line(8, 6, 11, 16)
    c.line(11, 16, 14, 8)
    c.line(14, 8, 16, 11)
    c.line(16, 11, 19, 11)
    icons[6] = ("Packet Monitor", c)

    # 7: ICON_PROBE_SNIFF (Radar reticle / Drone eye)
    c = Canvas22()
    c.circle(11, 11, 8)
    c.circle(11, 11, 4)
    c.line(11, 1, 11, 5)
    c.line(11, 17, 11, 21)
    c.line(1, 11, 5, 11)
    c.line(17, 11, 21, 11)
    c.set(11, 11)
    icons[7] = ("Probe Sniff", c)

    # 8: ICON_SCANNERS (Search Magnifier)
    c = Canvas22()
    c.circle(9, 9, 6)
    c.line(14, 14, 20, 20)
    c.line(15, 14, 20, 19)
    icons[8] = ("Scanners", c)

    # 9: ICON_CC_SKIMMERS (Credit Card with Chip)
    c = Canvas22()
    c.rect(2, 4, 18, 14)
    c.line(2, 8, 20, 8)
    c.rect(5, 11, 4, 4)
    c.line(12, 14, 17, 14)
    icons[9] = ("CC Skimmers", c)

    # 10: ICON_SNIFFERS (Surveillance Eye)
    c = Canvas22()
    c.circle(11, 11, 3)
    c.line(2, 11, 6, 6); c.line(6, 6, 16, 6); c.line(16, 6, 20, 11)
    c.line(2, 11, 6, 16); c.line(6, 16, 16, 16); c.line(16, 16, 20, 11)
    icons[10] = ("Sniffers", c)

    # 11: ICON_WIFI (Modern 3-Arc Wi-Fi)
    c = Canvas22()
    c.line(3, 5, 18, 5)
    c.set(2, 6); c.set(19, 6)
    c.line(6, 9, 15, 9)
    c.set(5, 10); c.set(16, 10)
    c.line(8, 13, 13, 13)
    c.set(7, 14); c.set(14, 14)
    c.fill_rect(10, 17, 2, 2)
    icons[11] = ("WiFi", c)

    # 12: ICON_BEACON_SPAM (Multi-wave Broadcast)
    c = Canvas22()
    c.circle(11, 11, 2)
    c.circle(11, 11, 5)
    c.circle(11, 11, 9)
    icons[12] = ("Beacon Spam", c)

    # 13: ICON_RICK_ROLL (Music Note)
    c = Canvas22()
    c.circle(6, 16, 2)
    c.circle(15, 13, 2)
    c.line(8, 16, 8, 5)
    c.line(17, 13, 17, 2)
    c.line(8, 5, 17, 2)
    c.line(8, 7, 17, 4)
    icons[13] = ("Music Note", c)

    # 14: ICON_REBOOT (Circular Refresh)
    c = Canvas22()
    c.circle(11, 11, 7)
    c.fill_rect(8, 4, 6, 5, 0) # break top
    c.line(14, 2, 14, 6); c.line(10, 6, 14, 6)
    icons[14] = ("Reboot", c)

    # 15: ICON_GENERAL_APPS / GAMES (D-Pad Controller)
    c = Canvas22()
    c.rect(2, 5, 18, 12)
    # D-pad cross on left
    c.line(6, 8, 6, 14)
    c.line(4, 11, 8, 11)
    # Action buttons on right
    c.circle(15, 9, 1)
    c.circle(17, 12, 1)
    icons[15] = ("Games / Apps", c)

    # 16: ICON_UPDATE (Download Arrow)
    c = Canvas22()
    c.line(11, 2, 11, 14)
    c.line(7, 10, 11, 14); c.line(15, 10, 11, 14)
    c.line(4, 17, 17, 17)
    c.line(4, 19, 17, 19)
    icons[16] = ("Update", c)

    # 17: ICON_DEVICE / GUIDE (Open Guide Book)
    c = Canvas22()
    c.rect(3, 4, 16, 14)
    c.line(11, 4, 11, 18)
    c.line(5, 7, 9, 7)
    c.line(5, 10, 9, 10)
    c.line(13, 7, 17, 7)
    c.line(13, 10, 17, 10)
    icons[17] = ("Device / Guide", c)

    # 18: ICON_DEVICE_INFO (Info Circle)
    c = Canvas22()
    c.circle(11, 11, 8)
    c.fill_rect(10, 6, 2, 2)
    c.line(11, 10, 11, 16)
    c.line(10, 10, 12, 10)
    c.line(9, 16, 13, 16)
    icons[18] = ("Info", c)

    # 19: ICON_SD_UPDATE (SD with arrow)
    c = Canvas22()
    c.rect(3, 3, 14, 16)
    c.line(10, 6, 10, 14)
    c.line(7, 11, 10, 14); c.line(13, 11, 10, 14)
    icons[19] = ("SD Update", c)

    # 20: ICON_WEB_UPDATE (Globe Network)
    c = Canvas22()
    c.circle(11, 11, 8)
    c.line(3, 11, 19, 11)
    c.line(11, 3, 11, 19)
    c.circle(11, 11, 4)
    icons[20] = ("Web / Globe", c)

    # 21: ICON_EAPOL (Key Handshake)
    c = Canvas22()
    c.circle(7, 11, 4)
    c.line(11, 11, 19, 11)
    c.line(16, 11, 16, 14)
    c.line(18, 11, 18, 14)
    icons[21] = ("EAPOL Key", c)

    # 22: ICON_STATUS_BAT (Battery Status)
    c = Canvas22()
    c.rect(2, 6, 16, 10)
    c.line(19, 9, 19, 13)
    c.fill_rect(5, 8, 3, 6)
    c.fill_rect(9, 8, 3, 6)
    c.fill_rect(13, 8, 3, 6)
    icons[22] = ("Battery Status", c)

    # 23: ICON_STATUS_SD (MicroSD Card)
    c = Canvas22()
    c.line(5, 3, 13, 3)
    c.line(13, 3, 17, 7)
    c.line(17, 7, 17, 19)
    c.line(17, 19, 5, 19)
    c.line(5, 19, 5, 3)
    c.line(7, 6, 7, 9)
    c.line(10, 6, 10, 9)
    c.line(13, 7, 13, 9)
    icons[23] = ("Status SD", c)

    # 24: ICON_PWNAGOTCHI (Cyber Mascot Face)
    c = Canvas22()
    c.rect(3, 5, 16, 12)
    c.line(6, 9, 8, 9)  # eye
    c.line(14, 9, 16, 9) # eye
    c.line(9, 13, 13, 13) # smile
    icons[24] = ("Pwnagotchi", c)

    # 25: ICON_SHUTDOWN (Power button)
    c = Canvas22()
    c.circle(11, 12, 6)
    c.fill_rect(9, 5, 4, 4, 0)
    c.line(11, 4, 11, 11)
    icons[25] = ("Shutdown", c)

    # 26: ICON_BEACON_LIST (List Stack)
    c = Canvas22()
    c.line(4, 5, 18, 5)
    c.line(4, 9, 18, 9)
    c.line(4, 13, 18, 13)
    c.line(4, 17, 18, 17)
    icons[26] = ("List", c)

    # 27: ICON_GENERATE (Sparkle / Star)
    c = Canvas22()
    c.line(11, 2, 11, 20)
    c.line(2, 11, 20, 11)
    c.line(5, 5, 17, 17)
    c.line(5, 17, 17, 5)
    icons[27] = ("Generate", c)

    # 28: ICON_CLEAR_ICO (Trash Can)
    c = Canvas22()
    c.rect(5, 7, 12, 13)
    c.line(3, 5, 19, 5)
    c.line(9, 3, 13, 3)
    c.line(8, 10, 8, 17)
    c.line(11, 10, 11, 17)
    c.line(14, 10, 14, 17)
    icons[28] = ("Clear / Trash", c)

    # 29: ICON_KEYBOARD_ICO (Keyboard)
    c = Canvas22()
    c.rect(2, 5, 18, 12)
    c.line(4, 8, 6, 8); c.line(8, 8, 10, 8); c.line(12, 8, 14, 8); c.line(16, 8, 18, 8)
    c.line(4, 11, 6, 11); c.line(8, 11, 10, 11); c.line(12, 11, 14, 11); c.line(16, 11, 18, 11)
    c.line(6, 14, 16, 14) # spacebar
    icons[29] = ("Keyboard", c)

    # 30: ICON_JOIN_WIFI (Link / Chain)
    c = Canvas22()
    c.line(5, 11, 17, 11)
    c.circle(5, 11, 3)
    c.circle(17, 11, 3)
    icons[30] = ("Join WiFi", c)

    # 31: ICON_LANGUAGE (Globe Translate)
    c = Canvas22()
    c.circle(11, 11, 8)
    c.line(3, 11, 19, 11)
    c.line(11, 3, 11, 19)
    icons[31] = ("Language", c)

    # 32: ICON_STATUS_GPS (Crosshairs)
    c = Canvas22()
    c.circle(11, 11, 7)
    c.circle(11, 11, 3)
    c.line(11, 2, 11, 20)
    c.line(2, 11, 20, 11)
    icons[32] = ("Status GPS", c)

    # 33: ICON_GPS_MENU (Compass Rose)
    c = Canvas22()
    c.circle(11, 11, 8)
    c.line(11, 5, 13, 11); c.line(13, 11, 11, 11); c.line(11, 11, 9, 11); c.line(9, 11, 11, 5)
    c.line(11, 17, 13, 11); c.line(9, 11, 11, 17)
    icons[33] = ("Compass", c)

    # 34: ICON_DISABLE_TOUCH (Padlock / Secure)
    c = Canvas22()
    c.rect(4, 9, 14, 11)
    c.line(7, 9, 7, 5); c.line(7, 5, 15, 5); c.line(15, 5, 15, 9)
    c.circle(11, 14, 1)
    c.line(11, 15, 11, 17)
    icons[34] = ("Lock / Security", c)

    # 35: ICON_FLIPPER (Transceiver Beacon)
    c = Canvas22()
    c.rect(5, 5, 12, 14)
    c.line(8, 2, 8, 5) # antenna
    c.circle(11, 10, 2)
    c.line(8, 15, 14, 15)
    icons[35] = ("Flipper", c)

    # 36: ICON_BLANK (Empty)
    c = Canvas22()
    icons[36] = ("Blank", c)

    # 37: ICON_PINESCAN_SNIFF (Antenna Tree)
    c = Canvas22()
    c.line(11, 2, 11, 20)
    c.line(6, 6, 16, 6)
    c.line(8, 10, 14, 10)
    c.line(9, 14, 13, 14)
    icons[37] = ("Pinescan", c)

    # 38: ICON_JOINED (Shield Checkmark)
    c = Canvas22()
    c.line(3, 4, 18, 4)
    c.line(18, 4, 18, 11); c.line(18, 11, 11, 19); c.line(11, 19, 3, 11); c.line(3, 11, 3, 4)
    c.line(7, 11, 10, 14); c.line(10, 14, 15, 8)
    icons[38] = ("Shield Check", c)

    # 39: ICON_FORCE (Target Cross)
    c = Canvas22()
    c.circle(11, 11, 7)
    c.line(11, 6, 11, 16)
    c.line(6, 11, 16, 11)
    icons[39] = ("Force", c)

    # 40: ICON_FUNNY_BEACON (Ghost / Stealth)
    c = Canvas22()
    c.line(4, 9, 4, 18); c.line(17, 9, 17, 18)
    c.line(4, 9, 8, 4); c.line(8, 4, 13, 4); c.line(13, 4, 17, 9)
    c.line(4, 18, 7, 15); c.line(7, 15, 11, 18); c.line(11, 18, 14, 15); c.line(14, 15, 17, 18)
    c.set(7, 9); c.set(14, 9)
    icons[40] = ("Ghost", c)

    # 41: ICON_FLOCK (Dual Antennas)
    c = Canvas22()
    c.line(6, 6, 6, 18); c.circle(6, 5, 2)
    c.line(15, 6, 15, 18); c.circle(15, 5, 2)
    c.line(2, 18, 20, 18)
    icons[41] = ("Flock", c)

    # 42: ICON_BRIGHTNESS (Sun with Rays)
    c = Canvas22()
    c.circle(11, 11, 4)
    c.line(11, 2, 11, 4); c.line(11, 18, 11, 20)
    c.line(2, 11, 4, 11); c.line(18, 11, 20, 11)
    c.line(5, 5, 6, 6); c.line(16, 16, 17, 17)
    c.line(5, 17, 6, 16); c.line(16, 6, 17, 5)
    icons[42] = ("Brightness", c)

    # 43: ICON_SETTINGS (Precision 6-Tooth Gear)
    c = Canvas22()
    c.circle(11, 11, 6)
    c.circle(11, 11, 2)
    # 6 teeth
    c.fill_rect(10, 2, 3, 3)   # top
    c.fill_rect(10, 17, 3, 3)  # bot
    c.fill_rect(2, 10, 3, 3)   # left
    c.fill_rect(17, 10, 3, 3)  # right
    c.fill_rect(4, 4, 3, 3)    # top-left
    c.fill_rect(15, 15, 3, 3)  # bot-right
    icons[43] = ("Settings Gear", c)

    # 44: ICON_CAMERA (SLR Camera with Lens)
    c = Canvas22()
    c.rect(2, 6, 18, 13)
    c.circle(11, 12, 4)
    c.circle(11, 12, 1) # aperture
    c.line(6, 4, 10, 4); c.line(6, 4, 6, 6); c.line(10, 4, 10, 6) # top prism
    c.set(16, 4) # flash dot
    icons[44] = ("Camera", c)

    # 45: ICON_SUBGHZ (433MHz Transmission Tower)
    c = Canvas22()
    c.line(11, 4, 6, 20)
    c.line(11, 4, 16, 20)
    c.line(8, 11, 14, 11)
    c.line(7, 16, 15, 16)
    c.circle(11, 3, 1)
    # pulse arcs
    c.set(7, 2); c.set(6, 3); c.set(6, 5)
    c.set(15, 2); c.set(16, 3); c.set(16, 5)
    icons[45] = ("Sub-GHz 433MHz", c)

    # 46: ICON_IR (Infrared Diode)
    c = Canvas22()
    c.line(6, 16, 16, 16)
    c.line(6, 16, 11, 8); c.line(16, 16, 11, 8)
    c.line(6, 8, 16, 8)
    c.line(11, 8, 11, 3)
    c.line(11, 16, 11, 20)
    # radiating photon rays
    c.line(15, 5, 19, 3); c.line(18, 5, 19, 3)
    c.line(14, 9, 18, 7); c.line(17, 9, 18, 7)
    icons[46] = ("Infrared", c)

    # 47: ICON_SD_CARD (MicroSD Card Silhouette)
    c = Canvas22()
    c.line(5, 2, 13, 2)
    c.line(13, 2, 17, 6)
    c.line(17, 6, 17, 20)
    c.line(17, 20, 5, 20)
    c.line(5, 20, 5, 2)
    c.line(7, 5, 7, 8)
    c.line(9, 5, 9, 8)
    c.line(11, 5, 11, 8)
    c.line(13, 6, 13, 8)
    c.line(15, 7, 15, 8)
    icons[47] = ("MicroSD Card", c)

    # 48: ICON_BATTERY (Horizontal Battery Cell)
    c = Canvas22()
    c.rect(2, 6, 16, 10)
    c.line(19, 9, 19, 13)
    c.fill_rect(5, 8, 3, 6)
    c.fill_rect(9, 8, 3, 6)
    c.fill_rect(13, 8, 3, 6)
    icons[48] = ("Battery", c)

    # 49: ICON_GALLERY (Photo Frame with Mountains)
    c = Canvas22()
    c.rect(2, 3, 18, 16)
    c.circle(6, 7, 2) # sun
    # mountains
    c.line(3, 16, 8, 10); c.line(8, 10, 13, 16)
    c.line(11, 16, 15, 12); c.line(15, 12, 19, 16)
    icons[49] = ("Gallery", c)

    # 50: ICON_DRONE (Quadcopter Drone)
    c = Canvas22()
    c.circle(11, 11, 3) # central hub
    c.line(5, 5, 17, 17) # diagonal arm 1
    c.line(5, 17, 17, 5) # diagonal arm 2
    # 4 rotors
    c.line(2, 5, 8, 5)
    c.line(14, 5, 20, 5)
    c.line(2, 17, 8, 17)
    c.line(14, 17, 20, 17)
    icons[50] = ("Drone Radar", c)

    # 51: ICON_FOXHUNT (Tactical Signal Radar)
    c = Canvas22()
    c.circle(11, 11, 8)
    c.circle(11, 11, 4)
    c.line(11, 11, 17, 6) # sweep line
    c.fill_rect(14, 5, 2, 2) # target pip
    icons[51] = ("Fox Hunt", c)

    # 52: ICON_NOTEPAD (Document Sheet)
    c = Canvas22()
    c.rect(4, 2, 14, 18)
    c.line(7, 6, 15, 6)
    c.line(7, 10, 15, 10)
    c.line(7, 14, 12, 14)
    icons[52] = ("Notepad", c)

    # 53: ICON_GAMES (Retro Arcade)
    c = Canvas22()
    c.rect(2, 5, 18, 12)
    c.line(6, 8, 6, 14)
    c.line(4, 11, 8, 11)
    c.circle(14, 9, 1)
    c.circle(16, 12, 1)
    icons[53] = ("Retro Games", c)

    return icons

def main():
    icons = create_icons()
    max_id = max(icons.keys())
    
    # 1. Generate src/icon_assets.cpp
    cpp_lines = [
        '#include "icon_assets.h"',
        '',
        '// High-Definition Outline Icon Matrix (22x22 XBM, 66 bytes per icon)',
        '// Standard XBM: Bit 1 = Foreground, Bit 0 = Background (Transparent)',
        '// Designed for clean 1-to-1.5px geometry (Lucide/Feather icon system)',
        f'PROGMEM const unsigned char axion_icons[{max_id + 1}][66] = {{'
    ]

    for i in range(max_id + 1):
        name, canvas = icons.get(i, ("Reserved", Canvas22()))
        bytes_data = canvas.to_xbm_bytes()
        hex_strs = [f"0x{b:02X}" for b in bytes_data]
        cpp_lines.append(f"    // [{i}] {name}")
        cpp_lines.append("    {" + ", ".join(hex_strs[:11]) + ",")
        cpp_lines.append("     " + ", ".join(hex_strs[11:22]) + ",")
        cpp_lines.append("     " + ", ".join(hex_strs[22:33]) + ",")
        cpp_lines.append("     " + ", ".join(hex_strs[33:44]) + ",")
        cpp_lines.append("     " + ", ".join(hex_strs[44:55]) + ",")
        cpp_lines.append("     " + ", ".join(hex_strs[55:]) + "}" + ("," if i < max_id else ""))
    
    cpp_lines.append("};")
    cpp_lines.append("")
    CPP_OUT.write_text("\n".join(cpp_lines), encoding="utf-8")
    print(f"Wrote {CPP_OUT} with {max_id + 1} icons.")

    # 2. Generate icons_data.js
    js_lines = [
        '// AUTO-GENERATED from src/icon_assets.cpp by tools/build_modern_icons.py',
        'const iconsData = ['
    ]
    for i in range(max_id + 1):
        name, canvas = icons.get(i, ("Reserved", Canvas22()))
        bytes_data = canvas.to_xbm_bytes()
        js_lines.append("  [" + ",".join(str(b) for b in bytes_data) + "],")
    js_lines.append("];")
    js_lines.append("")
    JS_OUT.write_text("\n".join(js_lines), encoding="utf-8")
    print(f"Wrote {JS_OUT} with {max_id + 1} icons.")

if __name__ == "__main__":
    main()
