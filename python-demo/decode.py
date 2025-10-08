import base64
import binascii
import sys

OUT_PATH = "debug_image.jpg"


def read_base64(path):
    with open(path, "r", encoding="utf-8") as infile:
        return infile.read()


def decode_to_file(b64_text, out_path):
    clean = "".join(b64_text.split())
    pad_len = (-len(clean)) % 4
    if pad_len:
        clean += "=" * pad_len

    raw = base64.b64decode(clean)

    with open(out_path, "wb") as outfile:
        outfile.write(raw)

    return raw


def main():
    if len(sys.argv) < 2:
        print("Usage: python decode.py <base64_text_file>")
        sys.exit(1)

    input_path = sys.argv[1]
    jpeg_b64 = read_base64(input_path)
    raw = decode_to_file(jpeg_b64, OUT_PATH)

    print(f"Saved image to: {OUT_PATH}")
    print(f"Size: {len(raw)} bytes")
    print("First 4 bytes:", binascii.hexlify(raw[:4]).decode())
    print("Last 2 bytes:", binascii.hexlify(raw[-2:]).decode())

    try:
        from PIL import Image

        with Image.open(OUT_PATH) as image:
            print("Format:", image.format)
            print("Dimensions:", image.size)
    except Exception as exc:
        print("Warning: could not open image:", exc)


if __name__ == "__main__":
    main()
