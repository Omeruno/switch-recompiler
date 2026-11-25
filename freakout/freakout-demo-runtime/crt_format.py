"""Formatting for the MSVCR71 cdecl varargs used by the demo."""
import math
import re
import struct


_FIELD = rb"%([-+ #0]*)([0-9]{0,6})(?:\.([0-9]{1,2}))?(l?[diuxX]|[fsc])"
_TOKEN = rb"(?:%%|" + _FIELD + rb")"
_FORMAT = re.compile(rb"(?:[^%]|" + _TOKEN + rb")*")
_CONVERSION = re.compile(_TOKEN)
_SCAN_TOKEN = rb"%(?:%|(?:[1-9][0-9]*)?(?:lf|[cfdisux]))"
_SCAN_FORMAT = re.compile(rb"(?:[^%]|" + _SCAN_TOKEN + rb")*")
_SCAN_PART = re.compile(_SCAN_TOKEN + rb"|\s+|[^%\s]")
_SCAN_VALUE = {
    b"f": rb"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?",
    b"d": rb"[+-]?[0-9]+",
    b"u": rb"[+-]?[0-9]+",
    b"i": rb"[+-]?(?:0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)",
    b"x": rb"[+-]?(?:0[xX])?[0-9a-fA-F]+",
    b"s": rb"\S+",
}


def fixed_decimal(value, places):
    # The legacy CRT rounds half away from zero. as_integer_ratio avoids
    # rounding the binary64 input a second time before the decimal conversion.
    if not math.isfinite(value):
        raise ValueError("non-finite value passed to sprintf")
    sign = "-" if math.copysign(1.0, value) < 0 else ""
    numerator, denominator = abs(value).as_integer_ratio()
    scale = 10 ** places
    magnitude, remainder = divmod(numerator * scale, denominator)
    magnitude += 2 * remainder >= denominator
    integral, fractional = divmod(magnitude, scale)
    if not places:
        return f"{sign}{integral}"
    return f"{sign}{integral}.{fractional:0{places}d}"


def format_cdecl(pattern, argument_cursor, read, read_string):
    if not _FORMAT.fullmatch(pattern):
        raise ValueError(f"unsupported sprintf pattern {pattern!r}")

    def convert(match):
        nonlocal argument_cursor
        token = match[0]
        if token == b"%%":
            return b"%"
        flags, width, precision, conversion = match.groups()
        conversion = conversion[-1:]
        width = int(width) if width else 0
        precision = int(precision) if precision is not None else None
        prefix = b""
        numeric = conversion not in (b"s", b"c")
        if conversion == b"f":
            value, = struct.unpack("<d", read(argument_cursor, 8))
            argument_cursor += 8
            digits = fixed_decimal(value, 6 if precision is None else precision).encode("ascii")
            if digits.startswith(b"-"):
                prefix, digits = b"-", digits[1:]
            if precision == 0 and b"#" in flags:
                digits += b"."
        else:
            value, = struct.unpack("<I", read(argument_cursor, 4))
            argument_cursor += 4
            if conversion == b"s":
                digits = read_string(value)[:precision]
            elif conversion == b"c":
                digits = bytes([value & 255])
            else:
                if conversion in (b"d", b"i") and value & 0x80000000:
                    prefix, value = b"-", (1 << 32) - value
                base = conversion.decode("ascii") if conversion in (b"x", b"X") else "d"
                digits = format(value, base).encode("ascii")
                if precision is not None:
                    digits = b"" if precision == 0 and value == 0 else digits.rjust(precision, b"0")
                if value and b"#" in flags and conversion in (b"x", b"X"):
                    prefix = b"0" + conversion
        if not prefix and conversion in (b"d", b"i", b"f"):
            prefix = b"+" if b"+" in flags else b" " if b" " in flags else b""
        if b"-" in flags:
            return (prefix + digits).ljust(width)
        if numeric and b"0" in flags and (conversion == b"f" or precision is None):
            return prefix + digits.rjust(max(0, width - len(prefix)), b"0")
        return (prefix + digits).rjust(width)

    return _CONVERSION.sub(convert, pattern)


def scan_cdecl(text, pattern, argument_cursor, read, write):
    if not _SCAN_FORMAT.fullmatch(pattern):
        raise ValueError(f"unsupported sscanf pattern {pattern!r}")
    assigned = 0
    cursor = 0
    for match in _SCAN_PART.finditer(pattern):
        token = match[0]
        if token.isspace():
            while cursor < len(text) and text[cursor:cursor + 1].isspace():
                cursor += 1
            continue
        if not token.startswith(b"%") or token == b"%%":
            if cursor == len(text):
                return assigned or -1
            if text[cursor:cursor + 1] != token[-1:]:
                break
            cursor += 1
            continue
        conversion = token[-1:]
        width = token[1:-1].removesuffix(b"l")
        if conversion != b"c":
            while cursor < len(text) and text[cursor:cursor + 1].isspace():
                cursor += 1
        if cursor == len(text):
            return assigned or -1
        limit = cursor + int(width) if width else len(text)
        if conversion == b"c":
            length = int(width) if width else 1
            value = text[cursor:cursor + length]
            if len(value) != length:
                return assigned or -1
            data = value
        else:
            parsed = re.match(_SCAN_VALUE[conversion], text[cursor:limit])
            if parsed is None:
                break
            value = parsed[0]
            if conversion == b"s":
                data = value + b"\0"
            elif conversion == b"f":
                number = float(value)
                encoded = "<d" if token.endswith(b"lf") else "<f"
                try:
                    data = struct.pack(encoded, number)
                except OverflowError:
                    data = struct.pack(encoded, math.copysign(math.inf, number))
            else:
                base = 16 if conversion == b"x" else 10
                if conversion == b"i":
                    digits = value.lstrip(b"+-").lower()
                    base = 16 if digits.startswith(b"0x") else 8 if digits.startswith(b"0") else 10
                data = struct.pack("<I", int(value, base) & 0xFFFFFFFF)
        address, = struct.unpack("<I", read(argument_cursor, 4))
        write(address, data)
        argument_cursor += 4
        cursor += len(value)
        assigned += 1
    return assigned
