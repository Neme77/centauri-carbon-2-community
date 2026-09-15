#!/usr/bin/env python3
"""
CC2 ELEG container utility.

Commands:
  inspect  - inspect an ELEG header and payload metadata
  verify   - verify payload SHA-256 and RSA PKCS#1 v1.5 signature
  pack     - build an ELEG container with an authorized signing key
  unpack   - AES-256-CBC decrypt and PKCS#7-unpad the payload

Use only signing/encryption keys you are authorized to use.
"""

import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

HEADER_SIZE = 0x200

OFF_MAGIC = 0x000
OFF_FLAGS = 0x004
OFF_VERSION = 0x005
OFF_SUBTYPE = 0x006
OFF_DATA_SIZE = 0x008
OFF_FILENAME = 0x010
OFF_CRYPT_START = 0x090
OFF_CRYPT_SPAN = 0x098
OFF_IV = 0x0A0
OFF_PAYLOAD_SIZE = 0x0B0
OFF_SHA256 = 0x0E0
OFF_RSA = 0x100

SHA_SIZE = 32
RSA_SIZE = 256
AES_BLOCK = 16


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u64(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


def c_string(buf, off, limit):
    raw = buf[off:limit]
    end = raw.find(b"\x00")
    if end >= 0:
        raw = raw[:end]
    return raw.decode("utf-8", errors="replace")


def read_package(path):
    with open(path, "rb") as f:
        data = f.read()

    if len(data) < HEADER_SIZE:
        raise ValueError("File too small to contain an ELEG header")

    return data[:HEADER_SIZE], data[HEADER_SIZE:], data


def parse_header(h):
    if len(h) != HEADER_SIZE:
        raise ValueError("Invalid ELEG header length")

    return {
        "magic": h[OFF_MAGIC:OFF_MAGIC + 4],
        "flags": h[OFF_FLAGS],
        "version": h[OFF_VERSION],
        "subtype": u16(h, OFF_SUBTYPE),
        "data_size": u64(h, OFF_DATA_SIZE),
        "filename": c_string(h, OFF_FILENAME, OFF_CRYPT_START),
        "crypt_start": u64(h, OFF_CRYPT_START),
        "crypt_span": u64(h, OFF_CRYPT_SPAN),
        "iv": h[OFF_IV:OFF_IV + 16],
        "payload_size": u64(h, OFF_PAYLOAD_SIZE),
        "sha256": h[OFF_SHA256:OFF_SHA256 + SHA_SIZE],
        "rsa_signature": h[OFF_RSA:OFF_RSA + RSA_SIZE],
    }


def inspect_package(path):
    h, payload, whole = read_package(path)
    x = parse_header(h)
    actual_sha = hashlib.sha256(payload).digest()

    print("ELEG package")
    print("=" * 72)
    print("File:              ", path)
    print("Container size:     ", len(whole))
    print("Header size:        ", HEADER_SIZE)
    print("Physical payload:   ", len(payload))
    print()
    print("Magic:              ", repr(x["magic"]))
    print("Flags/type:          0x%02X" % x["flags"])
    print("Version:             ", x["version"])
    print("Subtype:             ", x["subtype"])
    print("Embedded filename:   ", x["filename"])
    print()
    print("Data size @0x008:    ", x["data_size"])
    print("Crypto start @0x090:", x["crypt_start"])
    print("Crypto span @0x098: ", x["crypt_span"])
    print("Payload size @0x0B0:", x["payload_size"])
    print("IV @0x0A0:           ", x["iv"].hex())
    print()
    print("Header SHA256:       ", x["sha256"].hex())
    print("Actual SHA256:       ", actual_sha.hex())
    print("SHA256 match:        ", "YES" if actual_sha == x["sha256"] else "NO")
    print()
    print("RSA signature bytes: ", len(x["rsa_signature"]))

    return x, payload, whole


def verify_rsa(public_key, digest, signature):
    with tempfile.TemporaryDirectory(prefix="cc2sig_") as td:
        digest_path = os.path.join(td, "digest.bin")
        sig_path = os.path.join(td, "signature.bin")

        with open(digest_path, "wb") as f:
            f.write(digest)
        with open(sig_path, "wb") as f:
            f.write(signature)

        cmd = [
            "openssl", "pkeyutl",
            "-verify",
            "-pubin",
            "-inkey", public_key,
            "-sigfile", sig_path,
            "-in", digest_path,
            "-pkeyopt", "digest:sha256",
            "-pkeyopt", "rsa_padding_mode:pkcs1",
        ]
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        return result.returncode == 0, result.stdout.strip()


def sign_digest(private_key, digest):
    with tempfile.TemporaryDirectory(prefix="cc2sign_") as td:
        digest_path = os.path.join(td, "digest.bin")
        with open(digest_path, "wb") as f:
            f.write(digest)

        cmd = [
            "openssl", "pkeyutl",
            "-sign",
            "-inkey", private_key,
            "-in", digest_path,
            "-pkeyopt", "digest:sha256",
            "-pkeyopt", "rsa_padding_mode:pkcs1",
        ]
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode(errors="replace"))

        if len(result.stdout) != RSA_SIZE:
            raise ValueError(
                "Unexpected RSA signature size: %d (expected %d)"
                % (len(result.stdout), RSA_SIZE)
            )

        return result.stdout


def verify_package(path, public_key):
    x, payload, _ = inspect_package(path)
    failures = []

    if x["magic"] != b"ELEG":
        failures.append("invalid ELEG magic")

    if x["payload_size"] != len(payload):
        failures.append(
            "payload size mismatch: header=%d physical=%d"
            % (x["payload_size"], len(payload))
        )

    if x["crypt_start"] > len(payload):
        failures.append("crypto start exceeds payload")

    if x["crypt_start"] + x["crypt_span"] > len(payload):
        failures.append("crypto span exceeds payload")

    actual_sha = hashlib.sha256(payload).digest()
    if actual_sha != x["sha256"]:
        failures.append("SHA256 payload mismatch")

    print("-" * 72)
    print("RSA verification")
    rsa_ok, rsa_output = verify_rsa(
        public_key, x["sha256"], x["rsa_signature"]
    )
    print(rsa_output)
    print("RSA match:           ", "YES" if rsa_ok else "NO")

    if not rsa_ok:
        failures.append("RSA signature verification failed")

    print()
    print("-" * 72)

    if failures:
        print("RESULT: FAIL")
        for failure in failures:
            print(" -", failure)
        return 1

    print("RESULT: PASS")
    print()
    print("Structural observations:")
    print(" data_size delta:    ", len(payload) - x["data_size"])
    print(" crypt_start:        ", x["crypt_start"])
    print(" crypt_span:         ", x["crypt_span"])
    print(" payload_size:       ", x["payload_size"])
    return 0


def pkcs7_pad(data):
    n = AES_BLOCK - (len(data) % AES_BLOCK)
    return data + bytes([n]) * n


def pkcs7_unpad(data):
    if not data or len(data) % AES_BLOCK:
        raise ValueError("Invalid padded data length")

    n = data[-1]
    if n < 1 or n > AES_BLOCK:
        raise ValueError("Invalid PKCS#7 padding length")
    if data[-n:] != bytes([n]) * n:
        raise ValueError("Invalid PKCS#7 padding bytes")

    return data[:-n]


def aes256_cbc(data, key, iv, decrypt=False):
    if len(key) != 32:
        raise ValueError("AES key must be exactly 32 bytes")
    if len(iv) != 16:
        raise ValueError("AES IV must be exactly 16 bytes")
    if len(data) % AES_BLOCK:
        raise ValueError("AES input must be aligned to 16 bytes")

    cmd = [
        "openssl", "enc", "-aes-256-cbc",
        "-d" if decrypt else "-e",
        "-nopad",
        "-K", key.hex(),
        "-iv", iv.hex(),
    ]

    result = subprocess.run(
        cmd,
        input=data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    return result.stdout


def build_header(filename, data_size, ciphertext, iv, private_key, eleg_type=0x80):
    if not filename:
        raise ValueError("Embedded filename may not be empty")

    name = filename.encode("utf-8")
    max_name = OFF_CRYPT_START - OFF_FILENAME
    if len(name) >= max_name:
        raise ValueError(
            "Embedded filename is too long (%d bytes; max %d)"
            % (len(name), max_name - 1)
        )

    digest = hashlib.sha256(ciphertext).digest()
    signature = sign_digest(private_key, digest)

    h = bytearray(HEADER_SIZE)
    h[OFF_MAGIC:OFF_MAGIC + 4] = b"ELEG"
    if eleg_type not in (0x80, 0x83):
        raise ValueError("Unsupported encrypted ELEG type: 0x%02X" % eleg_type)
    h[OFF_FLAGS] = eleg_type
    h[OFF_VERSION] = 1
    struct.pack_into("<H", h, OFF_SUBTYPE, 2)
    struct.pack_into("<Q", h, OFF_DATA_SIZE, data_size)

    h[OFF_FILENAME:OFF_FILENAME + len(name)] = name
    h[OFF_FILENAME + len(name)] = 0

    struct.pack_into("<Q", h, OFF_CRYPT_START, 0)
    struct.pack_into("<Q", h, OFF_CRYPT_SPAN, len(ciphertext))
    h[OFF_IV:OFF_IV + 16] = iv
    struct.pack_into("<Q", h, OFF_PAYLOAD_SIZE, len(ciphertext))

    h[OFF_SHA256:OFF_SHA256 + SHA_SIZE] = digest
    h[OFF_RSA:OFF_RSA + RSA_SIZE] = signature
    return bytes(h)


def pack_package(cpio_path, output_path, aes_key_path, private_key,
                 embedded_filename=None, iv=None, eleg_type=0x80):
    with open(cpio_path, "rb") as f:
        plain = f.read()
    with open(aes_key_path, "rb") as f:
        aes_key = f.read()

    if len(aes_key) != 32:
        raise ValueError(
            "AES key is %d bytes; expected exactly 32" % len(aes_key)
        )

    if iv is None:
        iv = os.urandom(16)
    if len(iv) != 16:
        raise ValueError("IV must be exactly 16 bytes")

    if embedded_filename is None:
        embedded_filename = os.path.basename(cpio_path)
        if embedded_filename.endswith(".cpio"):
            embedded_filename = embedded_filename[:-5] + ".swu"

    padded = pkcs7_pad(plain)
    ciphertext = aes256_cbc(padded, aes_key, iv, decrypt=False)

    header = build_header(
        embedded_filename,
        len(plain),
        ciphertext,
        iv,
        private_key,
        eleg_type,
    )

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(ciphertext)

    print("Created ELEG package")
    print("=" * 72)
    print("Output:            ", output_path)
    print("Embedded filename: ", embedded_filename)
    print("ELEG type:         0x%02X" % eleg_type)
    print("CPIO size:         ", len(plain))
    print("Padding bytes:     ", len(padded) - len(plain))
    print("Ciphertext size:   ", len(ciphertext))
    print("IV:                ", iv.hex())
    print("SHA256:            ", hashlib.sha256(ciphertext).hexdigest())
    print("Signature bytes:   ", RSA_SIZE)
    return 0


def unpack_package(package_path, output_path, aes_key_path):
    h, payload, _ = read_package(package_path)
    x = parse_header(h)

    if x["magic"] != b"ELEG":
        raise ValueError("Invalid ELEG magic")
    if x["payload_size"] != len(payload):
        raise ValueError(
            "Payload size mismatch: header=%d physical=%d"
            % (x["payload_size"], len(payload))
        )

    with open(aes_key_path, "rb") as f:
        aes_key = f.read()

    if len(aes_key) != 32:
        raise ValueError("AES key must be exactly 32 bytes")

    if x["crypt_start"] != 0:
        raise ValueError(
            "Unsupported crypt_start=%d; currently expected 0"
            % x["crypt_start"]
        )
    if x["crypt_span"] != len(payload):
        raise ValueError(
            "Unsupported crypt_span=%d; expected %d"
            % (x["crypt_span"], len(payload))
        )

    decrypted = aes256_cbc(payload, aes_key, x["iv"], decrypt=True)
    plain = pkcs7_unpad(decrypted)

    if len(plain) != x["data_size"]:
        raise ValueError(
            "Decrypted data size mismatch: header=%d actual=%d"
            % (x["data_size"], len(plain))
        )

    with open(output_path, "wb") as f:
        f.write(plain)

    print("Unpacked ELEG package")
    print("=" * 72)
    print("Input:             ", package_path)
    print("Output:            ", output_path)
    print("Embedded filename: ", x["filename"])
    print("Ciphertext size:   ", len(payload))
    print("Plain size:        ", len(plain))
    print("Removed padding:   ", len(decrypted) - len(plain))
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Inspect, verify, pack and unpack CC2 ELEG containers. "
            "Use only signing keys you are authorized to use."
        )
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("inspect", help="Inspect an ELEG package")
    p.add_argument("package")

    p = sub.add_parser("verify", help="Verify SHA256 and RSA signature")
    p.add_argument("package")
    p.add_argument("--public-key", required=True)

    p = sub.add_parser("pack", help="Build an ELEG package")
    p.add_argument("cpio")
    p.add_argument("output")
    p.add_argument("--aes-key", required=True)
    p.add_argument("--private-key", required=True)
    p.add_argument("--filename")
    p.add_argument(
        "--type", dest="eleg_type", default="0x80", choices=("0x80", "0x83"),
        help="encrypted ELEG type: 0x80 for SWU, 0x83 for OTA manifest",
    )
    p.add_argument(
        "--iv",
        help="16-byte AES IV represented by exactly 32 hexadecimal characters",
    )

    p = sub.add_parser("unpack", help="Decrypt and unpack an ELEG payload")
    p.add_argument("package")
    p.add_argument("output")
    p.add_argument("--aes-key", required=True)

    args = parser.parse_args()

    try:
        if args.command == "inspect":
            inspect_package(args.package)
            return 0

        if args.command == "verify":
            return verify_package(args.package, args.public_key)

        if args.command == "pack":
            iv = bytes.fromhex(args.iv) if args.iv else None
            if iv is not None and len(iv) != 16:
                raise ValueError("--iv must contain exactly 16 bytes")

            return pack_package(
                args.cpio,
                args.output,
                args.aes_key,
                args.private_key,
                args.filename,
                iv,
                int(args.eleg_type, 16),
            )

        if args.command == "unpack":
            return unpack_package(
                args.package,
                args.output,
                args.aes_key,
            )

    except Exception as e:
        print("ERROR:", e, file=sys.stderr)
        return 2

    return 2


if __name__ == "__main__":
    sys.exit(main())
