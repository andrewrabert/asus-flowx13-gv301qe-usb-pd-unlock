#!/usr/bin/env python3
"""Bounded passive KM003C capture; raw USB evidence, no PD trigger or USB reset.

Protocol source: okhsunrog/km003c-rs commit
c063388ecab1a169326de1b57e650a3a45fc94f6 (packet/message/pd.rs).
Only claims vendor interface 0, restoring its kernel driver on exit.
Requires PyUSB and write access to the meter's USB device node.
Monitor acceptance/status polling validated on the local meter; event capture
still needs a negotiation. Preserve raw frames before decoding.
"""
import argparse
import json
import struct
import time
from datetime import datetime, timezone

import usb.core
import usb.util


def capture(seconds, output):
    meters = list(usb.core.find(find_all=True, idVendor=0x5fc9, idProduct=0x0063))
    if len(meters) != 1:
        raise RuntimeError(f"Expected exactly one meter, found {len(meters)}")
    dev = meters[0]
    # Open before creating output or changing driver bindings; permission failure is inert.
    dev.get_active_configuration()
    detached = claimed = monitoring = False
    transaction = 0
    with open(output, 'x', buffering=1) as log:
        def record(**fields):
            log.write(json.dumps(dict(utc=datetime.now(timezone.utc).isoformat(), **fields)) + '\n')

        def exchange(op, attr=0):
            nonlocal transaction
            if (op, attr) not in {(2, 0), (3, 0), (16, 1), (17, 0), (12, 16)}:
                raise ValueError('Command outside passive-capture allowlist')
            transaction = (transaction + 1) % 256
            command = struct.pack('<BBH', op, transaction, attr << 1)
            record(tx=command.hex())
            dev.write(1, command, timeout=1000)
            # Upstream sends Disconnect without waiting for Accept. This meter
            # replies with its own Disconnect/id1, rather than echoing our ID.
            if op == 3:
                return None
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                raw = bytes(dev.read(0x81, 65536, timeout=1000))
                record(rx=raw.hex())
                if op == 2 and raw and raw[0] & 127 == 3:
                    raise RuntimeError('Meter requested reconnect')
                if len(raw) >= 4 and raw[1] == transaction:
                    kind = raw[0] & 127
                    if kind not in (5, 65):
                        raise RuntimeError(f'Unexpected response type {kind:#x}')
                    if op != 12 and kind != 5:
                        raise RuntimeError('Expected command acceptance')
                    return raw
            raise TimeoutError('No matching response')

        try:
            if dev.is_kernel_driver_active(0):
                dev.detach_kernel_driver(0)
                detached = True
            usb.util.claim_interface(dev, 0)
            claimed = True
            # Disconnect replies may remain queued from the previous session.
            # Drain before allocating IDs, without resetting USB or the meter.
            for _ in range(16):
                try:
                    stale = bytes(dev.read(0x81, 65536, timeout=25))
                    record(stale_rx=stale.hex())
                except usb.core.USBTimeoutError:
                    break
            else:
                raise RuntimeError('Receive queue did not become idle')
            # Upstream also bounds Connect retries at three: the meter can
            # respond with Disconnect when an old session has not closed yet.
            for attempt in range(3):
                try:
                    exchange(2)
                    break
                except (RuntimeError, usb.core.USBTimeoutError) as error:
                    record(connect_attempt=attempt + 1, error=str(error))
                    if attempt == 2:
                        raise
                    time.sleep(0.1)
            monitoring = True  # Cleanup even if enable acceptance is lost.
            exchange(16, 1)
            print('Passive PD capture active', flush=True)
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                exchange(12, 16)
                time.sleep(0.02)
        finally:
            if monitoring:
                for op in (17, 3):
                    try:
                        exchange(op)
                    except Exception as error:
                        record(cleanup_error=str(error), opcode=op)
            if claimed:
                usb.util.release_interface(dev, 0)
            if detached:
                dev.attach_kernel_driver(0)
            usb.util.dispose_resources(dev)
            record(cleanup_complete=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('--seconds must be 1..3600')
    capture(args.seconds, args.output)
