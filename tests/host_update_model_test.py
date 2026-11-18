"""NOR behavior and failure injection against the actual prepared sectors."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from host_update_model import SECTORS, Operation, UpdateStopped, operations, simulate


class NorModel:
    def __init__(self, originals, fail_at=None):
        self.memory = {a: bytearray(b) for a, b in originals.items()}
        self.wel = False
        self.busy = False
        self.trace = []
        self.fail_at = fail_at

    def execute(self, op):
        self.trace.append(op)
        if len(self.trace) - 1 == self.fail_at:
            return False
        if self.busy and op.kind != 'wait_ready':
            raise RuntimeError('operation while flash is busy')
        base = op.address & ~4095
        offset = op.address - base
        if op.kind == 'identity_and_protection':
            return not self.wel
        if op.kind == 'write_enable':
            self.wel = True
        elif op.kind == 'write_disable':
            self.wel = False
        elif op.kind == 'check_wel':
            return self.wel
        elif op.kind == 'check_wel_clear':
            return not self.wel
        elif op.kind == 'wait_ready':
            self.busy = False
        elif op.kind == 'compare':
            return self.memory[base][offset:offset + len(op.data)] == op.data
        elif op.kind == 'erase':
            if not self.wel or offset or base not in SECTORS:
                raise RuntimeError('invalid erase')
            self.memory[base][:] = b'\xff' * 4096
            self.wel, self.busy = False, True
        elif op.kind == 'program':
            if not self.wel or not 0 < len(op.data) <= 32:
                raise RuntimeError('invalid program')
            if (op.address & 255) + len(op.data) > 256:
                raise RuntimeError('page wrap')
            old = self.memory[base][offset:offset + len(op.data)]
            self.memory[base][offset:offset + len(op.data)] = bytes(a & b for a, b in zip(old, op.data))
            self.wel, self.busy = False, True
        else:
            raise RuntimeError('unknown operation')
        return True


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        p = ROOT / 'out/update_415/host_sector_bundle_v1'
        cls.old = {a: (p / f'{a:06x}.original.bin').read_bytes() for a in SECTORS}
        cls.new = {a: (p / f'{a:06x}.modified.UNAPPROVED.bin').read_bytes() for a in SECTORS}

    def test_complete_nor_update(self):
        device = NorModel(self.old)
        count = simulate(self.old, self.new, device.execute)
        self.assertEqual(device.memory, self.new)
        self.assertFalse(device.wel)
        self.assertEqual(count, len(device.trace))

    def test_every_operation_failure_stops_immediately(self):
        expected = list(operations(self.old, self.new))
        for index, op in enumerate(expected):
            with self.subTest(index=index, operation=op.kind):
                device = NorModel(self.old, index)
                with self.assertRaises(UpdateStopped) as caught:
                    simulate(self.old, self.new, device.execute)
                self.assertEqual(len(device.trace), index + 1)
                if op.kind == 'erase':
                    self.assertIn(op.address, caught.exception.dirty_sectors)

    def test_stale_second_sector_prevents_all_writes(self):
        device = NorModel(self.old)
        device.memory[SECTORS[1]][0] ^= 1
        with self.assertRaises(UpdateStopped):
            simulate(self.old, self.new, device.execute)
        self.assertTrue(all(op.kind in ('identity_and_protection', 'compare') for op in device.trace))

    def test_exception_after_erase_is_uncertain(self):
        device = NorModel(self.old)
        def execute(op):
            result = device.execute(op)
            if op.kind == 'erase':
                raise TimeoutError('completion lost')
            return result
        with self.assertRaises(UpdateStopped) as caught:
            simulate(self.old, self.new, execute)
        self.assertEqual(caught.exception.dirty_sectors, (SECTORS[0],))
        self.assertEqual(device.trace[-1].kind, 'erase')

    def test_silent_program_corruption_stops_before_next_write(self):
        device = NorModel(self.old)
        injected = False
        def execute(op):
            nonlocal injected
            result = device.execute(op)
            if op.kind == 'program' and not injected:
                base = op.address & ~4095
                device.memory[base][op.address - base] ^= 1
                injected = True
            return result
        with self.assertRaises(UpdateStopped) as caught:
            simulate(self.old, self.new, execute)
        self.assertEqual(caught.exception.operation.kind, 'compare')
        self.assertEqual(caught.exception.dirty_sectors, (SECTORS[0],))
        self.assertEqual(sum(op.kind == 'program' for op in device.trace), 1)

    def test_incomplete_erase_prevents_programming(self):
        device = NorModel(self.old)
        def execute(op):
            result = device.execute(op)
            if op.kind == 'erase':
                device.memory[op.address][200] = 0
            return result
        with self.assertRaises(UpdateStopped):
            simulate(self.old, self.new, execute)
        self.assertFalse(any(op.kind == 'program' for op in device.trace))


if __name__ == '__main__':
    unittest.main()
