#!/usr/bin/env python3
"""Decode captured KM003C PD status and raw events; retain wire bytes.

Framing follows km003c-rs c063388e. Does not infer a contract from ADC data.
"""
import json
import statistics
import struct
import sys


def decode(path):
    samples, events, errors = [], [], []
    for line in open(path):
        row = json.loads(line)
        if 'rx' not in row:
            continue
        raw = bytes.fromhex(row['rx'])
        if len(raw) < 4 or raw[0] & 127 != 65:
            continue
        try:
            offset = 4
            while offset < len(raw):
                if offset + 4 > len(raw):
                    raise ValueError('truncated extended header')
                ext = int.from_bytes(raw[offset:offset+4], 'little')
                attr, more, size = ext & 32767, bool(ext & 32768), ext >> 22
                offset += 4
                payload = raw[offset:offset+size]
                if len(payload) != size:
                    raise ValueError('truncated payload')
                offset += size
                if attr == 16:
                    if size < 12:
                        raise ValueError('truncated status')
                    ts, mv, ma, cc1, cc2 = struct.unpack_from('<IHhHH', payload)
                    samples.append(dict(utc=row['utc'], meter_ms=ts, volts=mv/1000,
                                        amps_signed=ma/1000, watts_abs=abs(mv*ma)/1e6))
                    pos = 12
                    while pos < size:
                        if pos + 6 > size:
                            raise ValueError('truncated event header')
                        h = payload[pos:pos+6]
                        pos += 6
                        event = dict(utc=row['utc'], raw_header=h.hex())
                        if h[0] == 0x45:
                            event.update(kind='connection_status', code=h[5],
                                         meter_ms=int.from_bytes(h[1:4], 'little'))
                        else:
                            length = (h[0] & 63) - 5
                            if length < 0 or pos + length > size:
                                raise ValueError('invalid event length')
                            wire = payload[pos:pos+length]
                            pos += length
                            event.update(kind='pd_message', sop=h[5], wire=wire.hex(),
                                         meter_ms=int.from_bytes(h[1:5], 'little'))
                            if len(wire) >= 2:
                                header = int.from_bytes(wire[:2], 'little')
                                event.update(message_type=header & 31,
                                             object_count=(header >> 12) & 7,
                                             extended=bool(header & 32768))
                        events.append(event)
                if not more:
                    break
        except (ValueError, struct.error) as error:
            errors.append(dict(utc=row['utc'], error=str(error), raw=raw.hex()))
    watts = [x['watts_abs'] for x in samples]
    return dict(samples=len(samples), events=events, errors=errors,
                power_watts=dict(min=min(watts), median=statistics.median(watts),
                                 max=max(watts)) if watts else None,
                first_sample=samples[0] if samples else None,
                last_sample=samples[-1] if samples else None)


if __name__ == '__main__':
    print(json.dumps(decode(sys.argv[1]), indent=2))
