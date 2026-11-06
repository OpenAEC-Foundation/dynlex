"""X11 interaction and pixel capture on private test displays."""
import ctypes as c
import ctypes.util
import struct
import time
import zlib

class XButton(c.Structure):
    _fields_ = [
        ("type", c.c_int), ("serial", c.c_ulong), ("send_event", c.c_int),
        ("display", c.c_void_p), ("window", c.c_ulong), ("root", c.c_ulong),
        ("subwindow", c.c_ulong), ("time", c.c_ulong),
        ("x", c.c_int), ("y", c.c_int), ("x_root", c.c_int), ("y_root", c.c_int),
        ("state", c.c_uint), ("button", c.c_uint), ("same_screen", c.c_int),
    ]


class XKey(c.Structure):
    _fields_ = [("keycode" if name == "button" else name, kind) for name, kind in XButton._fields_]


class XClient(c.Structure):
    _fields_ = [
        ("type", c.c_int), ("serial", c.c_ulong), ("send_event", c.c_int),
        ("display", c.c_void_p), ("window", c.c_ulong),
        ("message_type", c.c_ulong), ("format", c.c_int), ("data", c.c_long * 5),
    ]


class XEvent(c.Union):
    _fields_ = [("button", XButton), ("key", XKey), ("client", XClient), ("pad", c.c_long * 24)]


class XImage(c.Structure):
    _fields_ = [
        ("width", c.c_int), ("height", c.c_int), ("xoffset", c.c_int), ("format", c.c_int),
        ("data", c.c_void_p), ("byte_order", c.c_int), ("bitmap_unit", c.c_int),
        ("bitmap_bit_order", c.c_int), ("bitmap_pad", c.c_int), ("depth", c.c_int),
        ("bytes_per_line", c.c_int), ("bits_per_pixel", c.c_int),
        ("red_mask", c.c_ulong), ("green_mask", c.c_ulong), ("blue_mask", c.c_ulong),
    ]


class XSizeHints(c.Structure):
    _fields_ = [("flags", c.c_long)] + [(name, c.c_int) for name in (
        "x", "y", "width", "height", "min_width", "min_height", "max_width", "max_height",
        "width_inc", "height_inc", "min_aspect_x", "min_aspect_y", "max_aspect_x", "max_aspect_y",
        "base_width", "base_height", "win_gravity",
    )]


def x11_library():
    library = ctypes.util.find_library("X11")
    if library is None:
        raise RuntimeError("The native UI test requires the X11 library used by GLFW")
    x = c.CDLL(library)
    signatures = {
        "XOpenDisplay": ([c.c_char_p], c.c_void_p),
        "XCloseDisplay": ([c.c_void_p], c.c_int),
        "XDefaultRootWindow": ([c.c_void_p], c.c_ulong),
        "XQueryTree": ([c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)], c.c_int),
        "XFetchName": ([c.c_void_p, c.c_ulong, c.POINTER(c.c_void_p)], c.c_int),
        "XFree": ([c.c_void_p], c.c_int),
        "XFlush": ([c.c_void_p], c.c_int),
        "XWarpPointer": ([c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_int, c.c_int], c.c_int),
        "XSendEvent": ([c.c_void_p, c.c_ulong, c.c_int, c.c_long, c.POINTER(XEvent)], c.c_int),
        "XKeysymToKeycode": ([c.c_void_p, c.c_ulong], c.c_uint),
        "XSetInputFocus": ([c.c_void_p, c.c_ulong, c.c_int, c.c_ulong], c.c_int),
        "XResizeWindow": ([c.c_void_p, c.c_ulong, c.c_uint, c.c_uint], c.c_int),
        "XMoveResizeWindow": ([c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint], c.c_int),
        "XGetWMNormalHints": ([c.c_void_p, c.c_ulong, c.POINTER(XSizeHints), c.POINTER(c.c_long)], c.c_int),
        "XGetGeometry": ([c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_uint), c.POINTER(c.c_uint), c.POINTER(c.c_uint), c.POINTER(c.c_uint)], c.c_int),
        "XTranslateCoordinates": ([c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_ulong)], c.c_int),
        "XInternAtom": ([c.c_void_p, c.c_char_p, c.c_int], c.c_ulong),
        "XGetImage": ([c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_ulong, c.c_int], c.POINTER(XImage)),
        "XGetPixel": ([c.POINTER(XImage), c.c_int, c.c_int], c.c_ulong),
        "XDestroyImage": ([c.POINTER(XImage)], c.c_int),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(x, name)
        function.argtypes, function.restype = arguments, result
    return x


def window_geometry(x, display, window):
    root, child = c.c_ulong(), c.c_ulong()
    left, top = c.c_int(), c.c_int()
    width, height, border, depth = (c.c_uint() for _ in range(4))
    if not x.XGetGeometry(display, window, c.byref(root), c.byref(left), c.byref(top), c.byref(width), c.byref(height), c.byref(border), c.byref(depth)):
        raise RuntimeError("Could not read the UI window geometry")
    if not x.XTranslateCoordinates(display, window, root.value, 0, 0, c.byref(left), c.byref(top), c.byref(child)):
        raise RuntimeError("Could not locate the UI window")
    return left.value, top.value, width.value, height.value


def window_size_hints(x, display, window):
    hints, supplied = XSizeHints(), c.c_long()
    if not x.XGetWMNormalHints(display, window, c.byref(hints), c.byref(supplied)):
        raise RuntimeError("The UI window has no normal size hints")
    return hints


def named_window(x, display, root, title):
    children = c.POINTER(c.c_ulong)()
    count, owner, parent = c.c_uint(), c.c_ulong(), c.c_ulong()
    if not x.XQueryTree(display, root, c.byref(owner), c.byref(parent), c.byref(children), c.byref(count)):
        raise RuntimeError("Could not inspect the private X server")
    try:
        for index in range(count.value):
            name = c.c_void_p()
            if x.XFetchName(display, children[index], c.byref(name)) and name.value:
                try:
                    if c.string_at(name) == title:
                        return children[index]
                finally:
                    x.XFree(name)
    finally:
        if children:
            x.XFree(children)
    return None


def screenshot(x, display, window, width, height):
    image = x.XGetImage(display, window, 0, 0, width, height, c.c_ulong(-1), 2)
    if not image:
        raise RuntimeError("Could not read the rendered UI")
    return image


def save_png(image, path):
    info = image.contents
    if (info.bits_per_pixel, info.byte_order, info.red_mask, info.green_mask, info.blue_mask) != (32, 0, 0xFF0000, 0xFF00, 0xFF):
        raise RuntimeError("The private X server must provide 32-bit little-endian RGB pixels")
    pixels = c.string_at(info.data, info.bytes_per_line * info.height)
    rows = bytearray()
    for y in range(info.height):
        rows.append(0)
        row = pixels[y * info.bytes_per_line:y * info.bytes_per_line + info.width * 4]
        for offset in range(0, len(row), 4):
            rows.extend((row[offset + 2], row[offset + 1], row[offset]))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", info.width, info.height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def click(x, display, root, window, horizontal, vertical):
    x.XWarpPointer(display, 0, window, 0, 0, 0, 0, horizontal, vertical)
    x.XFlush(display)
    time.sleep(0.15)
    for kind in (4, 5):
        event = XEvent()
        event.button = XButton(kind, 0, 1, display, window, root, 0, 0, horizontal, vertical, horizontal, vertical, 0, 1, 1)
        if not x.XSendEvent(display, window, 0, 1 << (kind - 2), c.byref(event)):
            raise RuntimeError("Could not send a mouse event to the private UI window")
        x.XFlush(display)
        time.sleep(0.2)



def press_key(x, display, root, window, keysym, release=True, interval=0.2):
    x.XSetInputFocus(display, window, 1, 0)
    keycode = x.XKeysymToKeycode(display, keysym)
    if keycode == 0:
        raise RuntimeError(f"The private X server has no key for {keysym}")
    for kind in ((2, 3) if release else (2,)):
        event = XEvent()
        # X11 autorepeat detection compares adjacent release/press timestamps.
        # Timestamp zero makes distinct synthetic taps look like key repeat.
        timestamp = int(time.monotonic() * 1000) & 0xFFFFFFFF
        event.key = XKey(kind, 0, 1, display, window, root, 0, timestamp, 0, 0, 0, 0, 0, keycode, 1)
        if not x.XSendEvent(display, window, 0, 1 << (kind - 2), c.byref(event)):
            raise RuntimeError("Could not send a key event to the private UI window")
        if interval:
            x.XFlush(display)
            time.sleep(interval)
    x.XFlush(display)


def close_window(x, display, window):
    event = XEvent()
    event.client.type, event.client.display, event.client.window = 33, display, window
    event.client.message_type = x.XInternAtom(display, b"WM_PROTOCOLS", 0)
    event.client.format = 32
    event.client.data[0] = x.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
    if not x.XSendEvent(display, window, 0, 0, c.byref(event)):
        raise RuntimeError("Could not close the private UI window")
    x.XFlush(display)


def image_region(image, left, top, width, height):
    info = image.contents
    pixels = c.string_at(info.data, info.bytes_per_line * info.height)
    return b"".join(pixels[y * info.bytes_per_line + left * 4:y * info.bytes_per_line + (left + width) * 4] for y in range(top, top + height))
