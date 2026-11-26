"""Inline pointer tables must not absorb following speed/animation bytes."""
from ingest_smwdisx import data_width_boundaries, non_code_regions


def main():
    # Exact authored shapes: Super Koopa (3 words), Firework (4), cluster
    # objects (9 including a null slot), and Koopa Kid (7).
    for bank, start, words, constant_bytes in (
            (2, 0xeb83, 3, 4), (3, 0xc81c, 4, 4),
            (2, 0xf825, 9, 6), (1, 0xfac7, 7, 32)):
        boundary = start+2*words
        end = boundary+constant_bytes
        lines = ['dw Handler']*words+['db $01,$ff', 'RTL']
        addresses = {(bank << 16) | (start+2*i): ('a', i+1)
                     for i in range(words)}
        addresses[(bank << 16) | boundary] = ('a', words+1)
        addresses[(bank << 16) | end] = ('a', words+2)
        kinds = {pc: 'data' for pc in addresses}
        kinds[(bank << 16) | end] = 'code'
        arch = {pc: '65816' for pc in addresses}
        stops = data_width_boundaries(addresses, {'a': lines}, {}, kinds, arch)
        assert stops == {(bank << 16) | boundary}
        assert non_code_regions(kinds, arch, stops) == [
            (bank, start, boundary), (bank, boundary, end)]
    print('SMWDisX pointer-table/constant boundaries: 4 authored shapes passed')


if __name__ == '__main__':
    main()
