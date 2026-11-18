"""Synthetic log validation; never represents a hardware measurement."""
import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('checker','tools/check_ec_read_probe.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
capsule=Path('bios/bios_version_415/GV301QEAS.415').read_bytes()
sample=b''.join(capsule[0x31800+a:0x31800+a+n] for a,n in m.REGIONS).hex()
log='result=0 ack=33 entry_sent=1 ack_seen=1 exit_consumed=1 cleanup_failed=0 sample_bytes=192\n'
log+=''.join(f'pass={i} id=ffffff sample={sample}\n' for i in range(2))
assert m.check(log,capsule)['sample_matches_stock_415']
for bad in [log.replace('exit_consumed=1','exit_consumed=0'),
            log.replace('cleanup_failed=0','cleanup_failed=1'),
            log.replace('sample_bytes=192','sample_bytes=96'),
            log.replace('result=0','result=2'),log[:log.index('pass=1')],
            log.replace(sample,'00'+sample[2:],1),
            log.replace(sample,'00'+sample[2:])]:
    try:m.check(bad,capsule)
    except ValueError:pass
    else:raise AssertionError('Accepted bad log')
print('PASS: stock sample fixture accepted; failed, incomplete, mismatched and altered samples rejected')
