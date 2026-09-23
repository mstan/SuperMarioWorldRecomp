"""A declared interpreter-only entry is covered; an absent/partial one is not."""
from apply_coop_hooks import apply, interpreted_entries, interpreted_coverage

def main():
    required={0x01808c,0x018127}
    body='L_8127:\n    cpu_trace_block(cpu, 0x018127);\n'
    patched,found=apply(body,required)
    assert found=={0x018127}
    assert apply(patched,required)==(patched,found)
    floor=interpreted_entries('{ 0x01808Cu, { NULL, NULL, NULL, NULL }, 0 },')
    assert not required-found-floor
    partial=interpreted_entries('{ 0x01808Cu, { NULL, fn, NULL, NULL }, 0 },')
    assert required-found-partial=={0x01808c}
    assert required-found-interpreted_entries('')=={0x01808c}
    owners={0xa21b:0xa1da,0xa28a:0xa1da}
    assert set(owners)<=interpreted_coverage('{0x00A1DAu,{NULL,NULL,NULL,NULL},0}',owners)
    assert not set(owners)&interpreted_coverage('{0x00A1DAu,{fn,NULL,NULL,NULL},0}',owners)
    assert not set(owners)&interpreted_coverage('',owners)
    print('co-op hooks: compiled idempotence and explicit interpreter coverage passed')

if __name__=='__main__':main()
