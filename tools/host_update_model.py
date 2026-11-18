"""Offline transaction model. No hardware implementation or deployment entry point.

Operations are descriptions consumed by tests, not SPI/MMIO commands. A future
writer still needs validated transport, deadlines, exclusion and recovery.
"""
from dataclasses import dataclass

SECTORS = (0x3D000, 0x42000)
SECTOR_SIZE = 4096
CHUNK_SIZE = 32  # Within the FIFO footprint already exercised by read probes.


@dataclass(frozen=True)
class Operation:
    kind: str
    address: int = 0
    data: bytes = b''


class UpdateStopped(RuntimeError):
    def __init__(self, operation, dirty_sectors):
        self.operation = operation
        self.dirty_sectors = tuple(sorted(dirty_sectors))
        super().__init__(f'{operation.kind} at {operation.address:#x}; '
                         f'possibly incomplete sectors: {self.dirty_sectors}')


def operations(originals, modified):
    """Yield a fixed-address workflow; comparisons must complete before advancing."""
    if set(originals) != set(SECTORS) or set(modified) != set(SECTORS):
        raise ValueError('requires exactly the two reviewed sectors')
    if any(len(x) != SECTOR_SIZE for x in (*originals.values(), *modified.values())):
        raise ValueError('wrong sector size')
    # Both originals must be checked before the FIRST erase.
    yield Operation('identity_and_protection')
    for address in SECTORS:
        yield Operation('compare', address, originals[address])
    for address in SECTORS:
        yield Operation('write_enable')
        yield Operation('check_wel')
        yield Operation('erase', address)
        yield Operation('wait_ready')
        yield Operation('compare', address, b'\xff' * SECTOR_SIZE)
        for offset in range(0, SECTOR_SIZE, CHUNK_SIZE):
            data = modified[address][offset:offset + CHUNK_SIZE]
            if data == b'\xff' * CHUNK_SIZE:
                continue
            yield Operation('write_enable')
            yield Operation('check_wel')
            yield Operation('program', address + offset, data)
            yield Operation('wait_ready')
            yield Operation('compare', address + offset, data)
        yield Operation('compare', address, modified[address])
    for address in SECTORS:
        yield Operation('compare', address, modified[address])
    yield Operation('write_disable')
    yield Operation('check_wel_clear')
    yield Operation('identity_and_protection')


def simulate(originals, modified, execute):
    """Stop on any failed/uncertain result, with no retry or rollback assumption.

    execute is a test double accepting an Operation and returning a boolean.
    Mark a sector dirty BEFORE erase: a failed command may have taken effect.
    Clear it only after full-sector verification of the intended replacement.
    """
    dirty = set()
    completed = 0
    for op in operations(originals, modified):
        if op.kind == 'erase':
            dirty.add(op.address)
        try:
            success = execute(op)
        except Exception as error:
            raise UpdateStopped(op, dirty) from error
        if success is not True:
            raise UpdateStopped(op, dirty)
        if (op.kind == 'compare' and op.address in SECTORS
                and op.data == modified[op.address]):
            dirty.discard(op.address)
        completed += 1
    return completed
