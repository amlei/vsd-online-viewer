#!/usr/bin/env python3
"""Chunk coverage census for Visio (.vsd) files.

Turns "we cannot fix unknown problems" into a measurable list: it walks every
chunk of the VisioDocument stream (same traversal libvisio does) and reports how
many chunks of each type libvisio never dispatches - i.e. everything that is
silently ignored today.

    python3 tools/vsd2svg/tools/census.py [--json] FILE.vsd [FILE.vsd ...]

Reads the CFB container itself; `olefile` is used when available but is not
required.
"""
import collections
import json
import struct
import sys


def read_cfb_stream(path, name):
    """Return a stream of the CFB/OLE container (minimal reader, no deps)."""
    try:
        import olefile  # type: ignore

        return olefile.OleFileIO(path).openstream(name).read()
    except ImportError:
        pass

    data = open(path, 'rb').read()
    if data[:8] != b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1':
        raise ValueError('not a CFB/OLE file')

    sector_size = 1 << struct.unpack_from('<H', data, 0x1E)[0]
    mini_size = 1 << struct.unpack_from('<H', data, 0x20)[0]
    num_fat = struct.unpack_from('<I', data, 0x2C)[0]
    dir_start = struct.unpack_from('<I', data, 0x30)[0]
    mini_cutoff = struct.unpack_from('<I', data, 0x38)[0]
    mini_fat_start = struct.unpack_from('<I', data, 0x3C)[0]
    difat_count = struct.unpack_from('<I', data, 0x48)[0]
    END = 0xFFFFFFFE

    def sector(n):
        off = (n + 1) * sector_size
        return data[off:off + sector_size]

    difat = list(struct.unpack_from('<109I', data, 0x4C))
    next_difat = struct.unpack_from('<I', data, 0x44)[0]
    while next_difat < 0xFFFFFFFA and difat_count > 0:
        entries = struct.unpack('<%dI' % (sector_size // 4), sector(next_difat))
        difat.extend(entries[:-1])
        next_difat = entries[-1]
        difat_count -= 1

    fat = []
    for s in difat:
        if s >= 0xFFFFFFFA:
            continue
        fat.extend(struct.unpack('<%dI' % (sector_size // 4), sector(s)))
        if len(fat) >= num_fat * (sector_size // 4):
            break

    def chain(start):
        out = []
        cur = start
        while cur < 0xFFFFFFFA and cur < len(fat):
            out.append(cur)
            cur = fat[cur]
        return out

    dir_bytes = b''.join(sector(s) for s in chain(dir_start))
    entries = [dir_bytes[i:i + 128] for i in range(0, len(dir_bytes), 128)]
    root = entries[0]
    mini_start = struct.unpack_from('<I', root, 0x74)[0]
    mini_stream = b''.join(sector(s) for s in chain(mini_start))

    target = None
    for entry in entries[1:]:
        name_len = struct.unpack_from('<H', entry, 0x40)[0]
        if name_len < 2:
            continue
        entry_name = entry[:name_len - 2].decode('utf-16-le', 'ignore')
        if entry_name == name:
            target = entry
            break
    if target is None:
        raise KeyError(f'stream {name!r} not found')

    start = struct.unpack_from('<I', target, 0x74)[0]
    size = struct.unpack_from('<Q', target, 0x78)[0]
    if size >= mini_cutoff:
        return b''.join(sector(s) for s in chain(start))[:size]

    mini_fat = []
    for s in chain(mini_fat_start):
        mini_fat.extend(struct.unpack('<%dI' % (sector_size // 4), sector(s)))
    out = bytearray()
    cur = start
    while cur < 0xFFFFFFFA and cur < len(mini_fat):
        off = cur * mini_size
        out += mini_stream[off:off + mini_size]
        cur = mini_fat[cur]
    return bytes(out[:size])

# libvisio 0.1.11, VSDParser::handleChunk dispatch table (see
# src/lib/VSDParser.cpp) - generated from the upstream source.
HANDLED_CHUNKS = {
    0x0C, 0x0D, 0x0E, 0x15, 0x16, 0x19, 0x1E, 0x1F, 0x2C, 0x2D, 0x32, 0x33,
    0x34, 0x46, 0x47, 0x48, 0x4A, 0x4E, 0x65, 0x66, 0x68, 0x69, 0x6A, 0x6B,
    0x6C, 0x6F, 0x83, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D,
    0x8F, 0x90, 0x92, 0x94, 0x95, 0x96, 0x97, 0x98, 0x9B, 0x9C, 0x9D, 0xA1,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xC1, 0xC3, 0xC9, 0xD1, 0xD7,
}

# [MS-VSD] / libvisio VSDDocumentStructure.h names, for readable output.
CHUNK_NAMES = {
    0x0C: 'FOREIGN_DATA', 0x0D: 'OLE_LIST', 0x0E: 'TEXT', 0x14: 'TRAILER_STREAM',
    0x15: 'PAGE', 0x16: 'COLORS', 0x18: 'FONT_LIST', 0x19: 'FONT_IX', 0x1A: 'STYLES',
    0x1D: 'STENCILS', 0x1E: 'STENCIL_PAGE', 0x1F: 'OLE_DATA', 0x27: 'PAGES',
    0x2C: 'NAME_LIST', 0x2D: 'NAME', 0x32: 'NAME_LIST2', 0x33: 'NAME2',
    0x34: 'NAMEIDX123', 0x46: 'PAGE_SHEET', 0x47: 'SHAPE_GROUP', 0x48: 'SHAPE_SHAPE',
    0x4A: 'STYLE_SHEET', 0x4D: 'SHAPE_GUIDE', 0x4E: 'SHAPE_FOREIGN', 0x64: 'SCRATCH_LIST',
    0x65: 'SHAPE_LIST', 0x66: 'FIELD_LIST', 0x68: 'PROP_LIST', 0x69: 'CHAR_LIST',
    0x6A: 'PARA_LIST', 0x6B: 'TABS_DATA_LIST', 0x6C: 'GEOM_LIST', 0x6D: 'CUST_PROPS_LIST',
    0x6E: 'ACT_ID_LIST', 0x6F: 'LAYER_LIST', 0x70: 'CTRL_LIST', 0x71: 'C_PNTS_LIST',
    0x72: 'CONNECT_LIST', 0x73: 'HYPER_LNK_LIST', 0x76: 'SMART_TAG_LIST',
    0x83: 'SHAPE_ID', 0x84: 'EVENT', 0x85: 'LINE', 0x86: 'FILL_AND_SHADOW',
    0x87: 'TEXT_BLOCK', 0x88: 'TABS_DATA_1', 0x89: 'GEOMETRY', 0x8A: 'MOVE_TO',
    0x8B: 'LINE_TO', 0x8C: 'ARC_TO', 0x8D: 'INFINITE_LINE', 0x8F: 'ELLIPSE',
    0x90: 'ELLIPTICAL_ARC_TO', 0x92: 'PAGE_PROPS', 0x93: 'STYLE_PROPS', 0x94: 'CHAR_IX',
    0x95: 'PARA_IX', 0x96: 'TABS_DATA_2', 0x97: 'TABS_DATA_3', 0x98: 'FOREIGN_DATA_TYPE',
    0x99: 'CONNECTION_POINTS', 0x9B: 'XFORM_DATA', 0x9C: 'TEXT_XFORM', 0x9D: 'XFORM_1D',
    0x9E: 'SCRATCH', 0xA0: 'PROTECTION', 0xA1: 'TEXT_FIELD', 0xA2: 'CONTROL_ANOTHER_TYPE',
    0xA4: 'MISC', 0xA5: 'SPLINE_START', 0xA6: 'SPLINE_KNOT', 0xA7: 'LAYER_MEMBERSHIP',
    0xA8: 'LAYER', 0xA9: 'ACT_ID', 0xAA: 'CONTROL', 0xB4: 'USER_DEFINED_CELLS',
    0xB5: 'TABS_DATA_4', 0xB6: 'CUSTOM_PROPS', 0xB7: 'RULER_GRID',
    0xBA: 'CONNECTION_POINTS_ANOTHER_TYPE', 0xBC: 'DOC_PROPS', 0xBD: 'IMAGE',
    0xBE: 'GROUP', 0xBF: 'LAYOUT', 0xC0: 'PAGE_LAYOUT_IX', 0xC1: 'POLYLINE_TO',
    0xC3: 'NURBS_TO', 0xC4: 'HYPERLINK', 0xC5: 'REVIEWER', 0xC6: 'ANNOTATION',
    0xC7: 'SMART_TAG_DEF', 0xC8: 'PRINT_PROPS', 0xC9: 'NAMEIDX', 0xD1: 'SHAPE_DATA',
    0xD7: 'FONTFACE', 0xD8: 'FONTFACES',
}

TRAILER_ALWAYS = {0x71, 0x70, 0x6B, 0x6A, 0x69, 0x66, 0x65, 0x2C}
TRAILER_LIST = {0x64, 0x65, 0x66, 0x69, 0x6A, 0x6B, 0x6F, 0x71,
                0x92, 0xA9, 0xB4, 0xB6, 0xB9, 0xC7}
NEVER_TRAILER = {0x1F, 0xC9, 0x2D, 0xD1}
VSD_PAGE = 0x15
VSD_COLORS = 0x16


def decompress(raw):
    if len(raw) < 2:
        return raw
    out = bytearray()
    window = bytearray(4096)
    pos = 0
    off = 0
    while off < len(raw):
        flag = raw[off]
        off += 1
        mask = 1
        for _ in range(8):
            if off >= len(raw):
                break
            if flag & mask:
                b = raw[off]
                off += 1
                window[pos & 4095] = b
                out.append(b)
                pos += 1
            else:
                if off > len(raw) - 2:
                    break
                a1, a2 = raw[off], raw[off + 1]
                off += 2
                length = (a2 & 15) + 3
                pointer = ((a2 & 0xF0) << 4) | a1
                pointer = pointer - 4078 if pointer > 4078 else pointer + 18
                for j in range(length):
                    window[(pos + j) & 4095] = window[(pointer + j) & 4095]
                    out.append(window[(pos + j) & 4095])
                pos += length
            mask <<= 1
    return bytes(out)


class Stream:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else (self.pos + off if whence == 1 else len(self.data) + off)
        self.pos = max(0, min(self.pos, len(self.data)))

    def end(self):
        return self.pos >= len(self.data)

    def read(self, n):
        if self.pos + n > len(self.data):
            raise EOFError
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def u8(self):
        return self.read(1)[0]

    def u16(self):
        return struct.unpack('<H', self.read(2))[0]

    def u32(self):
        return struct.unpack('<I', self.read(4))[0]

    def s32(self):
        return struct.unpack('<i', self.read(4))[0]


class Census:
    def __init__(self, main):
        self.main = main
        self.visited = set()
        self.counts = collections.Counter()
        self.levels = collections.defaultdict(collections.Counter)
        self.type_pages = collections.defaultdict(set)
        self.page = 0
        self.pending_unknown = 0

    def stream_for(self, ptr):
        data = self.main[ptr['offset']:ptr['offset'] + ptr['length']]
        return decompress(data) if ptr['format'] & 2 else data

    def read_pointer(self, st):
        typ = st.u32()
        st.seek(4, 1)
        offset = st.u32()
        length = st.u32()
        fmt = st.u16()
        return {'type': typ, 'offset': offset, 'length': length, 'format': fmt}

    def handle_streams(self, st, shift, level):
        try:
            st.seek(shift, 0)
            offset = st.u32()
            st.seek(offset + shift - 4, 0)
            list_size = st.u32()
            count = st.s32()
            st.seek(4, 1)
            ptrs = {}
            for i in range(count):
                p = self.read_pointer(st)
                if p['type']:
                    ptrs[i] = p
            order = [st.u32() for _ in range(list_size)] if list_size > 1 else []
        except EOFError:
            return
        for idx in (order or list(ptrs.keys())):
            if idx in ptrs:
                self.handle_stream(ptrs[idx], level + 1)

    def handle_stream(self, ptr, level):
        if ptr['type'] == VSD_PAGE:
            self.page += 1
        buf = self.stream_for(ptr)
        st = Stream(buf)
        shift = 4 if ptr['format'] & 2 else 0
        container = ptr['format'] >> 4
        if container in (0, 4, 5):
            self.handle_chunk(ptr['type'], level + 1)
            if container == 5 and ptr['type'] != VSD_COLORS and ptr['offset'] not in self.visited:
                self.visited.add(ptr['offset'])
                self.handle_streams(st, shift, level + 1)
        elif container in (8, 0xC, 0xD):
            self.handle_chunks(st, level + 1)

    def handle_chunks(self, st, level):
        while not st.end():
            if not self.chunk_header(st, level):
                return
            hdr = self.header
            self.handle_chunk(hdr['type'], level)
            st.seek(hdr['dataLength'] + hdr['trailer'] + st.pos, 0)

    def chunk_header(self, st, level):
        tmp = 0
        while not st.end() and not tmp:
            tmp = st.u8()
        if st.end():
            return False
        st.seek(-1, 1)
        hdr = {}
        hdr['type'] = st.u32()
        hdr['id'] = st.u32()
        hdr['list'] = st.u32()
        trailer = 0
        if hdr['list'] != 0 or hdr['type'] in TRAILER_ALWAYS:
            trailer += 8
        if (hdr['list'] != 0 or (level == 2 and self.pending_unknown == 0x55) or
                (level == 2 and self.pending_unknown == 0x54 and hdr['type'] == 0xAA) or
                (level == 3 and self.pending_unknown not in (0x50, 0x54))):
            trailer += 4
        hdr['dataLength'] = st.u32()
        hdr['level'] = st.u16()
        hdr['unknown'] = st.u8()
        self.pending_unknown = hdr['unknown']
        for c in TRAILER_LIST:
            if hdr['type'] == c and trailer not in (12, 4):
                trailer += 4
                break
        if hdr['type'] in NEVER_TRAILER:
            trailer = 0
        hdr['trailer'] = trailer
        self.header = hdr
        return True

    def handle_chunk(self, chunk_type, level):
        self.counts[chunk_type] += 1
        if chunk_type not in HANDLED_CHUNKS:
            self.levels[chunk_type][level] += 1
            self.type_pages[chunk_type].add(self.page)


def census(path):
    stream = read_cfb_stream(path, 'VisioDocument')
    trailer = {
        'type': struct.unpack_from('<I', stream, 0x24)[0],
        'offset': struct.unpack_from('<I', stream, 0x2C)[0],
        'length': struct.unpack_from('<I', stream, 0x30)[0],
        'format': struct.unpack_from('<H', stream, 0x34)[0],
    }
    c = Census(stream)
    c.handle_streams(Stream(c.stream_for(trailer)), 4, 0)
    return c


def main():
    argv = [a for a in sys.argv[1:] if a != '--json']
    as_json = '--json' in sys.argv[1:]
    if not argv:
        print(__doc__)
        return 2
    reports = []
    for path in argv:
        c = census(path)
        total = sum(c.counts.values())
        unhandled = sum(c.levels[t].total() for t in c.levels)
        report = {
            'file': path,
            'chunks': total,
            'notDispatched': unhandled,
            'ratio': round(unhandled / total, 5) if total else 0.0,
            'types': [
                {
                    'type': '0x%02x' % t,
                    'name': CHUNK_NAMES.get(t, '?'),
                    'count': counter.total(),
                    'levels': sorted(counter),
                    'pages': sorted(c.type_pages[t])[:20],
                }
                for t, counter in sorted(c.levels.items(), key=lambda kv: -kv[1].total())
            ],
            'pages': c.page,
        }
        reports.append(report)
        if as_json:
            continue
        print(f'== {path}')
        print(f'   chunks              : {total}')
        print(f'   not dispatched      : {unhandled} '
              f'({unhandled / total * 100:.2f}%) in {len(c.levels)} chunk types')
        if c.levels:
            print('   type                 name                  count   levels   first pages')
            for t, counter in sorted(c.levels.items(), key=lambda kv: -kv[1].total())[:15]:
                name = CHUNK_NAMES.get(t, '?')
                pages = sorted(c.type_pages[t])[:6]
                print('   0x%02x %-20s %7d   %-8s %s'
                      % (t, name, counter.total(), ','.join(str(x) for x in sorted(counter)), pages))
        print(f'   pages walked        : {c.page}')
    if as_json:
        print(json.dumps(reports, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
