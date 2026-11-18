"""Read-only audit of GV301QE 415 capsule signing coverage.

Requires Python cryptography. Mirrors SecSMIFlash RVA 0x2c04's region
selection and layout inclusion for this capsule; it is not a general parser.
Mutations exist only in memory. No firmware image is written.
"""
import hashlib
import json
import struct
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa, utils


def digest(image):
    payload, layout = struct.unpack_from("<HH", image, 0x1C)
    assert payload == 0x800 and layout == 0x458
    h = hashlib.sha256()
    regions = []
    for cursor in range(layout, payload - 23, 24):
        address, offset, size, kind, attributes = struct.unpack_from("<Q4I", image, cursor)
        if size == 0:
            break
        assert payload + offset + size <= len(image)
        if attributes & 0x200:
            h.update(image[payload + offset:payload + offset + size])
            regions.append([payload + offset, payload + offset + size])
    else:
        raise ValueError("Missing layout terminator")
    # This image has no optional header-in-hash flag (bit 1).
    assert not (struct.unpack_from("<I", image, 0x14)[0] & 2)
    h.update(image[layout:cursor + 24])
    return h.digest(), regions


def verifies(image, key):
    value, _ = digest(image)
    try:
        key.verify(image[0x148:0x248], value,
                   padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                               salt_length=padding.PSS.AUTO),
                   utils.Prehashed(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False


def main():
    root = Path(__file__).resolve().parents[1]
    path = root / "bios/bios_version_415/GV301QEAS.415"
    image = path.read_bytes()
    assert len(image) == 0x1000800
    key = rsa.RSAPublicNumbers(65537, int.from_bytes(image[0x48:0x148], "big")).public_key()
    original_valid = verifies(image, key)
    assert original_valid, "Stock capsule signature did not verify"
    value, regions = digest(image)
    probes = []
    for offset, expected in [(0x436FA, bytes.fromhex("d9 0b")),
                             (0x3DDEE, bytes.fromhex("4c 00 c0 08"))]:
        assert image[offset:offset + len(expected)] == expected
        changed = bytearray(image)
        changed[offset] ^= 1  # Coverage probe, NOT an executable patch.
        accepted = verifies(bytes(changed), key)
        assert not accepted
        probes.append({"capsule_offset": hex(offset), "original_bytes": expected.hex(),
                       "one_bit_mutation_signature_valid": accepted})
    print(json.dumps({"input": str(path.relative_to(root)),
                      "sha256": hashlib.sha256(image).hexdigest(),
                      "algorithm": "RSA-2048 PSS / SHA-256",
                      "stock_signature_valid_with_embedded_key": original_valid,
                      "signed_digest": value.hex(),
                      "signed_capsule_ranges_end_exclusive":
                          [[hex(a), hex(b)] for a, b in regions],
                      "layout_also_signed": True,
                      "coverage_probes": probes,
                      "limitation": "Does not establish installed trust-anchor policy or separate EC authentication."}, indent=2))


if __name__ == "__main__":
    main()
