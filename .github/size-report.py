"""The size of REAL.exe and what it is made of.

The executable gives the sizes of its sections and the libraries it imports;
the map file of the linker (/MAP) gives every symbol with its address and the
object file it comes from. The distance to the next symbol is the size of a
symbol, and the symbols are grouped by the library their name or their object
file belongs to. The groups are approximate (padding and unnamed data go to the
symbol before them), but they show where the bytes are.

The report is printed, written to the summary of the job and published as a
notice of the check, so that it can be read without the log of the job.

Usage: size-report.py REAL.exe REAL.map
"""

import os
import re
import struct
import sys

KB = 1024.0


def kb(value):
    return "{:.1f}".format(value / KB)


# ---------------------------------------------------------------- executable


def read_pe(path):
    with open(path, "rb") as stream:
        data = stream.read()

    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")

    sections_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)

    sections = []
    table = optional + optional_size
    for index in range(sections_count):
        header = table + index * 40
        name = data[header:header + 8].rstrip(b"\0").decode("ascii", "replace")
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, header + 8)
        sections.append((name, virtual_size, virtual_address, raw_size, raw_pointer))

    def offset(rva):
        for _, virtual_size, virtual_address, raw_size, raw_pointer in sections:
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                return raw_pointer + rva - virtual_address
        raise ValueError("address outside the sections: 0x{:x}".format(rva))

    def text_at(rva):
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii", "replace")

    imports = []
    import_rva, import_size = struct.unpack_from("<II", data, directories + 8)
    if import_rva != 0 and import_size != 0:
        descriptor = offset(import_rva)
        while True:
            lookup, _, _, name_rva, thunks = struct.unpack_from("<IIIII", data, descriptor)
            if name_rva == 0:
                break

            entry = offset(lookup if lookup != 0 else thunks)
            width = 8 if magic == 0x20B else 4
            count = 0
            while struct.unpack_from("<Q" if width == 8 else "<I", data, entry)[0] != 0:
                count += 1
                entry += width

            imports.append((text_at(name_rva), count))
            descriptor += 20

    return len(data), sections, imports


# ------------------------------------------------------------------- map file

SECTION_LINE = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})H\s+(\S+)\s+(CODE|DATA)\s*$")
SYMBOL_LINE = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})\s+(.+?)\s*$")


def read_map(path):
    # Section number -> [end offset, set of contribution names].
    sections = {}
    symbols = []

    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = SECTION_LINE.match(line)
            if match:
                number = int(match.group(1), 16)
                end = int(match.group(2), 16) + int(match.group(3), 16)
                entry = sections.setdefault(number, [0, set()])
                entry[0] = max(entry[0], end)
                entry[1].add(match.group(4).split("$")[0])
                continue

            match = SYMBOL_LINE.match(line)
            if match:
                number = int(match.group(1), 16)
                if number == 0:
                    continue

                tail = match.group(5).split()
                while len(tail) > 1 and tail[0] in ("f", "i"):
                    tail = tail[1:]

                symbols.append((number, int(match.group(2), 16), match.group(3), " ".join(tail)))

    # Publics and static symbols together, in the order of their addresses; a
    # symbol listed twice counts once.
    symbols = sorted(set(symbols))
    sized = []
    for index, (number, start, name, source) in enumerate(symbols):
        if index + 1 < len(symbols) and symbols[index + 1][0] == number:
            end = symbols[index + 1][1]
        else:
            end = sections.get(number, [start])[0]

        sized.append((number, max(0, end - start), name, source))

    return sections, sized


# ------------------------------------------------------------------- grouping

PARTS = [
    ("std::regex", re.compile(
        r"regex|\?\$_Matcher@|\?\$_Parser@|\?\$_Builder@|_Node_base|\?\$_Node_|_Root_node|_Bt_state|_Tgt_state")),
    ("iostream", re.compile(
        r"basic_ostream|basic_istream|basic_iostream|basic_streambuf|basic_ios@|ios_base|basic_filebuf|"
        r"basic_stringbuf|basic_stringstream|basic_ostringstream|basic_istringstream|basic_ifstream|"
        r"basic_ofstream|basic_fstream|_Iosb|_Fiopen|\?w?cout@|\?cerr@|\?cin@|\?clog@|_Winit|"
        r"streambuf_iterator")),
    ("locale", re.compile(
        r"locale|ctype|codecvt|numpunct|num_put|num_get|collate|moneypunct|money_get|money_put|time_get|"
        r"time_put|\?\$messages@|_Locinfo|_Lockit|_Yarn|_Facet_base|_Getcat|_Locimp|wstring_convert")),
    ("std::filesystem", re.compile(r"filesystem|__std_fs_")),
    ("nlohmann::json", re.compile(r"nlohmann")),
    ("spdlog", re.compile(r"spdlog")),
    ("fmt", re.compile(r"@fmt$|@fmt@|\bfmt@")),
]

CRT_LIBRARIES = ("libucrt", "libvcruntime", "libcmt", "msvcrt", "ucrt", "vcruntime", "libconcrt")


def part_of(name, source):
    # The scope of a decorated name is what comes before its first "@@": the
    # types of the parameters (std::string, json) come after it and say
    # nothing about where the code belongs.
    head = name.split("@@")[0]
    library = source.split(":")[0].lower() if ":" in source else ""

    if "miniant" in head:
        return "own code"

    for part, pattern in PARTS:
        if pattern.search(head):
            return part

    if library == "libcpmt" or library == "msvcprt":
        return "C++ library (other)"

    if library.startswith(CRT_LIBRARIES):
        return "C runtime"

    if library != "" or source.startswith("<"):
        return "imports, linker"

    if re.search(r"@std$|@std@", head):
        return "std templates"

    return "own code"


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    exe_path, map_path = sys.argv[1], sys.argv[2]
    lines = []

    size, pe_sections, imports = read_pe(exe_path)
    lines.append("REAL.exe: {:,} bytes ({} KB)".format(size, kb(size)).replace(",", " "))
    lines.append("Sections (KB): " + ", ".join(
        "{} {}".format(name, kb(raw_size)) for name, _, _, raw_size, _ in pe_sections))
    lines.append("DLLs: " + ", ".join("{} ({})".format(name, count) for name, count in imports))

    if os.path.exists(map_path):
        sections, symbols = read_map(map_path)

        # Code and read-only data make up the file; .data/.bss and the
        # unwind tables follow the code.
        wanted = {}
        for number, (end, names) in sections.items():
            if ".text" in names:
                wanted[number] = "code"
            elif ".rdata" in names:
                wanted[number] = "rdata"

        parts = {}
        objects = {}
        for number, length, name, source in symbols:
            kind = wanted.get(number)
            if kind is None:
                continue

            part = part_of(name, source)
            totals = parts.setdefault(part, {"code": 0, "rdata": 0})
            totals[kind] += length
            objects[source] = objects.get(source, 0) + length

        ordered = sorted(parts.items(), key=lambda item: -(item[1]["code"] + item[1]["rdata"]))
        lines.append("Parts, code + read-only data (KB): " + "; ".join(
            "{} {} + {}".format(part, kb(totals["code"]), kb(totals["rdata"])) for part, totals in ordered))

        own = sorted(
            ((source, length) for source, length in objects.items() if ":" not in source and source.endswith(".obj")),
            key=lambda item: -item[1])
        lines.append("Own objects (KB): " + "; ".join(
            "{} {}".format(source, kb(length)) for source, length in own[:12]))

        libraries = sorted(
            ((source, length) for source, length in objects.items() if ":" in source),
            key=lambda item: -item[1])
        lines.append("Library objects (KB): " + "; ".join(
            "{} {}".format(source, kb(length)) for source, length in libraries[:12]))
    else:
        lines.append("No map file: " + map_path)

    report = "\n".join(lines)
    print(report)

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write("# Size of REAL.exe\n\n```\n" + report + "\n```\n")

    if os.environ.get("GITHUB_ACTIONS"):
        message = report.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print("::notice title=Size of REAL.exe::" + message)

    return 0


if __name__ == "__main__":
    sys.exit(main())
