"""Native hook transfers preserve their generated frame and are idempotent."""
from apply_coop_hooks import apply, interpreted_entries, interpreted_coverage

def main():
    required={0x01808c,0x018127}
    body='L_8127:\n    cpu_trace_block(cpu, 0x018127);\n'
    patched,found=apply(body,required)
    assert found=={0x018127}
    assert apply(patched,required)==(patched,found)
    assert 'SmwCoopNativeTail(cpu, target, _entry_s, _hrv)' in patched
    assert 'interp_tier_dispatch_tail' not in patched
    native = ('RecompReturn f(CpuState *cpu) {\n'
              '  L_B457_M1X1:\n    cpu_trace_block(cpu, 0x01B457);\n'
              '  L_B4B0_M1X1:\n    return 0;\n}\n')
    generated, _ = apply(native,{0x01b457})
    assert 'goto L_B4B0_M1X1;' in generated
    assert 'cpu->m_flag == 1 && cpu->x_flag == 1' in generated
    floor=interpreted_entries('{ 0x01808Cu, { NULL, NULL, NULL, NULL }, 0 },')
    assert not required-found-floor
    partial=interpreted_entries('{ 0x01808Cu, { NULL, fn, NULL, NULL }, 0 },')
    assert required-found-partial=={0x01808c}
    assert required-found-interpreted_entries('')=={0x01808c}
    owners={0xa21b:0xa1da,0xa28a:0xa1da}
    assert set(owners)<=interpreted_coverage('{0x00A1DAu,{NULL,NULL,NULL,NULL},0}',owners)
    assert not set(owners)&interpreted_coverage('{0x00A1DAu,{fn,NULL,NULL,NULL},0}',owners)
    assert not set(owners)&interpreted_coverage('',owners)
    print('co-op hooks: native local transfer, paired tail frame and idempotence passed')

if __name__=='__main__':main()
