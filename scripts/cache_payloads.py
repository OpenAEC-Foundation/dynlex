"""Inspect the compiler's definite-length CBOR structured cache payloads."""

import struct


def decode_cbor(data):
    position = 0

    def take(count):
        nonlocal position
        if count > len(data) - position:
            raise ValueError("truncated cache CBOR")
        value = data[position:position + count]
        position += count
        return value

    def argument(extra):
        if extra < 24:
            return extra
        if extra < 28:
            return int.from_bytes(take(1 << (extra - 24)), "big")
        raise ValueError("invalid cache CBOR length")

    def value():
        token = take(1)[0]
        major, extra = token >> 5, token & 31
        if major == 7:
            if extra in (20, 21, 22):
                return {20: False, 21: True, 22: None}[extra]
            if extra in (25, 26, 27):
                return struct.unpack({25: ">e", 26: ">f", 27: ">d"}[extra],
                                     take(1 << (extra - 24)))[0]
            raise ValueError("invalid cache CBOR scalar")
        count = argument(extra)
        if major == 0:
            return count
        if major == 1:
            return -1 - count
        if major == 2:
            return {"bytes": list(take(count)), "subtype": None}
        if major == 3:
            return take(count).decode("utf-8")
        if major == 4:
            return [value() for _ in range(count)]
        if major == 5:
            result = {}
            for _ in range(count):
                key = value()
                if not isinstance(key, str) or key in result:
                    raise ValueError("invalid cache CBOR object key")
                result[key] = value()
            return result
        if major == 6:
            binary = value()
            if not isinstance(binary, dict) or set(binary) != {"bytes", "subtype"}:
                raise ValueError("invalid cache CBOR binary subtype")
            binary["subtype"] = count
            return binary
        raise ValueError("invalid cache CBOR type")

    result = value()
    if position != len(data):
        raise ValueError("trailing cache CBOR data")
    return result


def read_payload(path):
    return decode_cbor(path.read_bytes().split(b"\n", 2)[2])
