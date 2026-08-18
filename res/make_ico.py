import struct

W = H = 32
pixels = []  # list of (r,g,b,a), bottom-up rows (BMP style)

def rounded(x, y, w, h, r):
    if x < r and y < h - r:
        return (x - r) * (x - r) + (y - (h - r)) * (y - (h - r)) <= r * r
    if x >= w - r and y < h - r:
        return (x - (w - r)) * (x - (w - r)) + (y - (h - r)) * (y - (h - r)) <= r * r
    if x < r and y >= r:
        return (x - r) * (x - r) + (y - r) * (y - r) <= r * r
    if x >= w - r and y >= r:
        return (x - (w - r)) * (x - (w - r)) + (y - r) * (y - r) <= r * r
    return True

for y in range(H - 1, -1, -1):      # bottom-up
    for x in range(W):
        if 4 <= x <= 27 and 4 <= y <= 27 and rounded(x - 4, y - 4, 24, 24, 4):
            if x >= 20 and y >= 20:
                pixels.append((200, 210, 230, 255))   # light fold
            else:
                pixels.append((245, 248, 255, 255))   # white paper
        elif 20 <= x <= 27 and 4 <= y <= 10:
            pixels.append((70, 120, 220, 255))        # blue header
        else:
            pixels.append((0, 0, 0, 0))               # transparent

def setpx(px, py, r, g, b, a):
    if 4 <= px <= 27 and 4 <= py <= 27:
        idx = (H - 1 - py) * W + px
        pixels[idx] = (r, g, b, a)

for ly in range(11, 27, 3):
    for lx in range(7, 23):
        if lx < 20 or ly < 20:
            setpx(lx, ly, 90, 110, 150, 255)

out = bytearray()
out += struct.pack('<HHH', 0, 1, 1)        # ICONDIR
img_offset = 6 + 16
bih = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 32, 0, 0, 0, 0, 0, 0)
img = bytearray(bih)
for (r, g, b, a) in pixels:
    img += struct.pack('<BBBB', b, g, r, a)
out += struct.pack('<BBBBHHII', W, H, 0, 0, 1, 32, len(img), img_offset)
out += img
with open("app.ico", "wb") as f:
    f.write(out)
print("ico bytes:", len(out))
