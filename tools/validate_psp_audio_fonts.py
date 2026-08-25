#!/usr/bin/env python3
"""Validate the big-endian soundfont records consumed by the PSP audio loader."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from version import version_config


def main() -> int:
    version = sys.argv[1] if len(sys.argv) > 1 else "n64-us"
    config = version_config.load_version_config(version)
    base = ROOT / "extracted" / version / "baserom"
    code = (base / "code").read_bytes()
    audiobank = (base / "Audiobank").read_bytes()
    audiotable = (base / "Audiotable").read_bytes()
    code_vram = config.dmadata_segments["code"].vram

    def table(symbol):
        offset = config.variables[symbol] - code_vram
        count = struct.unpack_from(">h", code, offset)[0]
        return [struct.unpack_from(">IIbbhhh", code, offset + 0x10 + i * 0x10) for i in range(count)]

    fonts = table("gSoundFontTable")
    banks = table("gSampleBankTable")
    errors = []
    samples = {}
    refs = 0

    def real_bank(bank_id):
        seen = set()
        while bank_id != 0xFF and banks[bank_id][1] == 0:
            if bank_id in seen:
                raise RuntimeError("sample bank alias loop")
            seen.add(bank_id)
            bank_id = banks[bank_id][0]
        return bank_id

    for font_id, entry in enumerate(fonts):
        font_start, font_size, _, _, short1, short2, num_sfx = entry
        data = memoryview(audiobank)[font_start : font_start + font_size]
        bank_ids = [(short1 >> 8) & 0xFF, short1 & 0xFF]
        num_inst = min((short2 >> 8) & 0xFF, 126)
        num_drums = short2 & 0xFF

        def check_range(offset, size, what):
            if offset < 0 or offset + size > font_size:
                errors.append(f"font {font_id}: {what} 0x{offset:X}+0x{size:X} outside 0x{font_size:X}")
                return False
            return True

        def be32(offset):
            if not check_range(offset, 4, "u32"):
                return 0
            return struct.unpack_from(">I", data, offset)[0]

        def sample(sample_offset, source):
            nonlocal refs
            if sample_offset == 0:
                return
            refs += 1
            if not check_range(sample_offset, 0x10, f"sample from {source}"):
                return
            packed, sample_addr, loop, book = struct.unpack_from(">IIII", data, sample_offset)
            codec = (packed >> 28) & 7
            medium = (packed >> 26) & 3
            size = packed & 0xFFFFFF
            samples.setdefault((font_id, sample_offset), (codec, medium, sample_addr, size, loop, book))
            if not check_range(loop, 0x10, f"loop for sample 0x{sample_offset:X}"):
                return
            count = be32(loop + 8)
            if count and not check_range(loop, 0x30, f"loop state for sample 0x{sample_offset:X}"):
                return
            if not check_range(book, 8, f"book for sample 0x{sample_offset:X}"):
                return
            order, predictors = struct.unpack_from(">II", data, book)
            if order > 16 or predictors > 64:
                errors.append(f"font {font_id}: bad book order={order} predictors={predictors} at 0x{book:X}")
            check_range(book, 8 + 16 * order * predictors, f"book data for sample 0x{sample_offset:X}")
            if medium in (0, 1):
                bank_id = real_bank(bank_ids[medium])
                if bank_id == 0xFF:
                    errors.append(f"font {font_id}: sample 0x{sample_offset:X} uses missing bank {medium}")
                else:
                    bank_start, bank_size = banks[bank_id][0], banks[bank_id][1]
                    if sample_addr + size > bank_size:
                        errors.append(
                            f"font {font_id}: sample 0x{sample_offset:X} bank {bank_id} "
                            f"0x{sample_addr:X}+0x{size:X} outside 0x{bank_size:X}"
                        )
                    if bank_start + sample_addr + size > len(audiotable):
                        errors.append(f"font {font_id}: sample 0x{sample_offset:X} outside Audiotable")
            else:
                errors.append(f"font {font_id}: raw sample 0x{sample_offset:X} has medium {medium}")

        drums_offset = be32(0)
        if num_drums and check_range(drums_offset, 4 * num_drums, "drum pointer list"):
            for i in range(num_drums):
                drum = be32(drums_offset + 4 * i)
                if drum and check_range(drum, 0x10, f"drum {i}"):
                    sample(be32(drum + 4), f"drum {i}")
                    check_range(be32(drum + 0xC), 4, f"drum {i} envelope")

        sfx_offset = be32(4)
        if num_sfx and check_range(sfx_offset, 8 * num_sfx, "sfx list"):
            for i in range(num_sfx):
                sample(be32(sfx_offset + 8 * i), f"sfx {i}")

        if check_range(8, 4 * num_inst, "instrument pointer list"):
            for i in range(num_inst):
                inst = be32(8 + 4 * i)
                if not inst or not check_range(inst, 0x20, f"instrument {i}"):
                    continue
                lo, hi = data[inst + 1], data[inst + 2]
                check_range(be32(inst + 4), 4, f"instrument {i} envelope")
                if lo:
                    sample(be32(inst + 8), f"instrument {i} low")
                sample(be32(inst + 0x10), f"instrument {i} normal")
                if hi != 0x7F:
                    sample(be32(inst + 0x18), f"instrument {i} high")

    print(f"validated {len(fonts)} fonts, {refs} tuned-sample references, {len(samples)} unique samples")
    print(f"Audiobank=0x{len(audiobank):X} Audiotable=0x{len(audiotable):X}")
    if errors:
        print(f"errors: {len(errors)}")
        print("\n".join(errors[:100]))
        return 1
    print("all font-relative pointers and sample-bank ranges are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
