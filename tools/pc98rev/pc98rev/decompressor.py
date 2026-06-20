"""Brandish LZSS/Falcom2 decompressor (ported from brandish2/src/Decompression.cpp)."""

from __future__ import annotations


class Decompressor:
    def __init__(self, data: bytes) -> None:
        self.src: bytes = data
        self.si: int = 0       # source index
        self.dst: bytearray = bytearray()
        self.bytes_written: int = 0

    def decompress(self) -> bytes:
        if len(self.src) < 4:
            return b""

        # Dispatch based on byte at offset 2
        if self.src[2] != 0:
            self._unpack()
        else:
            self._unpack2()

        self.si += 1  # trailing byte
        return bytes(self.dst[: self.bytes_written])

    # ---- UNPACK: LZSS-style block handler ----
    def _unpack(self) -> None:
        block_start = self.si
        end_offset = self.src[self.si] | (self.src[self.si + 1] << 8)
        self.si += 2
        block_end = block_start + end_offset

        while self.si < block_end:
            control = self.src[self.si]
            self.si += 1

            if (control & 0x80) == 0:
                # Not a backref
                if (control & 0x40) != 0:
                    # RLE fill
                    if (control & 0x10) != 0:
                        # Extended RLE (0x50-0x5F)
                        lo = self.src[self.si]
                        fill_byte = self.src[self.si + 1]
                        self.si += 2
                        length = (((control & 0x0F) << 8) | lo) + 4
                    else:
                        # Short RLE (0x40-0x4F)
                        fill_byte = self.src[self.si]
                        self.si += 1
                        length = (control & 0x0F) + 4

                    self._ensure_capacity(self.bytes_written + length)
                    for i in range(length):
                        self.dst[self.bytes_written + i] = fill_byte
                    self.bytes_written += length

                elif (control & 0x20) != 0:
                    # Extended literal (0x20-0x3F)
                    length = (((control & 0x1F) << 8) | self.src[self.si]) & 0x1FFF
                    self.si += 1
                    self._copy_literal(length)

                else:
                    # Short literal (0x00-0x1F)
                    length = control & 0x1F
                    if length > 0:
                        self._copy_literal(length)
            else:
                # Backref (0x80-0xFF)
                offset = (((control & 0x1F) << 8) | self.src[self.si]) & 0x1FFF
                self.si += 1
                length = ((control >> 5) & 3) + 4

                # Continuation bytes (0x60-0x7F)
                while self.si < block_end and (self.src[self.si] & 0xE0) == 0x60:
                    length += self.src[self.si] & 0x1F
                    self.si += 1

                self._backref(offset, length)

    def _copy_literal(self, length: int) -> None:
        self._ensure_capacity(self.bytes_written + length)
        for i in range(length):
            self.dst[self.bytes_written + i] = self.src[self.si + i]
        self.bytes_written += length
        self.si += length

    def _backref(self, offset: int, length: int) -> None:
        self._ensure_capacity(self.bytes_written + length)
        for i in range(length):
            self.dst[self.bytes_written + i] = self.dst[self.bytes_written - offset + i]
        self.bytes_written += length

    # ---- UNPACK2: Falcom2 bit-packed handler ----
    def _unpack2(self) -> None:
        self.si += 2  # skip first 2 bytes

        # Read initial bit buffer (byte-swapped uint16)
        word = self.src[self.si] | (self.src[self.si + 1] << 8)
        self.si += 2
        self._bit_buffer = ((word & 0xFF) << 8) | ((word >> 8) & 0xFF)
        self._bits_left = 8

        while True:
            if not self._get_bit():
                # Literal
                self._ensure_capacity(self.bytes_written + 1)
                self.dst[self.bytes_written] = self.src[self.si]
                self.bytes_written += 1
                self.si += 1
            else:
                if not self._get_bit():
                    # Short backref
                    offset = self.src[self.si]
                    self.si += 1
                    self._decode_and_copy_run(offset)
                else:
                    # Extended command
                    offset_hi = self._read_bits_msb(5)
                    offset_lo = self.src[self.si]
                    self.si += 1
                    offset = (offset_hi << 8) | offset_lo

                    if offset == 0:
                        # End of block
                        return
                    elif offset == 1:
                        # RLE fill
                        extended = self._get_bit()
                        if not extended:
                            length = self._read_bits_msb(4) + 14
                        else:
                            len_hi = self._read_bits_msb(4)
                            len_lo = self.src[self.si]
                            self.si += 1
                            length = ((len_hi << 8) | len_lo) + 14

                        fill_byte = self.src[self.si]
                        self.si += 1

                        self._ensure_capacity(self.bytes_written + length)
                        for i in range(length):
                            self.dst[self.bytes_written + i] = fill_byte
                        self.bytes_written += length
                    else:
                        # Extended backref
                        self._decode_and_copy_run(offset)

    def _decode_and_copy_run(self, offset: int) -> None:
        run = 2
        if not self._get_bit():
            run = 3
            if not self._get_bit():
                run = 4
                if not self._get_bit():
                    run = 5
                    if not self._get_bit():
                        if not self._get_bit():
                            run = self.src[self.si] + 14
                            self.si += 1
                        else:
                            run = self._read_bits_msb(3) + 6

        self._ensure_capacity(self.bytes_written + run)
        for i in range(run):
            self.dst[self.bytes_written + i] = self.dst[self.bytes_written - offset + i]
        self.bytes_written += run

    # ---- Bit reader ----
    def _get_bit(self) -> bool:
        if self._bits_left == 0:
            self._bit_buffer = self.src[self.si] | (self.src[self.si + 1] << 8)
            self.si += 2
            self._bits_left = 16
        bit = (self._bit_buffer & 1) != 0
        self._bit_buffer >>= 1
        self._bits_left -= 1
        return bit

    def _read_bits_msb(self, count: int) -> int:
        result = 0
        for _ in range(count):
            result = (result << 1) | (1 if self._get_bit() else 0)
        return result

    # ---- Buffer management ----
    def _ensure_capacity(self, needed: int) -> None:
        if needed > len(self.dst):
            grow = max(needed, len(self.dst) * 2, 65536)
            self.dst.extend(bytearray(grow - len(self.dst)))
