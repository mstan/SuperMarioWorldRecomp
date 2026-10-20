#!/usr/bin/env python3
"""Qualify a normal-speed interval of a muted, captured SMW audio probe.

The full-run benchmark remains authoritative for its own interval. A turbo
run intentionally cannot pass its zero-loss whole-run gate; this tool reports
post-turbo recovery separately and never rewrites the original result.
"""
import argparse
import array
import csv
import json
from pathlib import Path
import sys
import wave


def check(directory, start, end):
    original = json.loads((directory/'result.json').read_text())
    rows = list(csv.DictReader((directory/'audio-frames.csv').open()))
    first = next(r for r in rows if int(r['frame']) == start and r['phase'] == 'before_input')
    if end is None:
        end = max(int(r['frame']) for r in rows)
    last = next(r for r in rows if int(r['frame']) == end and r['phase'] == 'after_guest')
    interval = [r for r in rows if start <= int(r['frame']) <= end]
    if end < start or any(int(r['turbo']) for r in interval):
        raise ValueError('Acceptance interval must contain only normal-speed frames')
    delta = {key: int(last[key])-int(first[key])
             for key in ('produced', 'consumed', 'dropped', 'audible', 'underflows')}
    with wave.open(str(directory/'output.wav'), 'rb') as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        begin, finish = int(first['output_frames']), int(last['output_frames'])
        assert 0 <= begin < finish <= wav.getnframes(), 'Capture is incomplete'
        wav.setpos(begin)
        samples = array.array('h', wav.readframes(finish-begin))
    if sys.byteorder != 'little':
        samples.byteswap()
    peak = max(map(abs, samples))
    rms = (sum(v*v for v in samples)/len(samples))**0.5
    frames = end-start+1
    checks = {
        'process_completed': (not original.get('timed_out', False)
                              and original['returncode'] in (0, 3)
                              and original.get('benchmark', {}).get('frames', 0) > end),
        'no_dispatch_misses': not original['dispatch_misses'],
        'no_audible_loss': delta['audible'] == 0,
        'no_underflows': delta['underflows'] == 0,
        'guest_audio_produced': delta['produced'] >= frames*500,
        'audio_consumed': delta['consumed'] >= frames*500,
        'sustained_non_silent_output': peak > 256 and rms > 10,
    }
    report = dict(status='passed' if all(checks.values()) else 'failed',
                  start_frame=start, end_frame=end, checks=checks, counters=delta,
                  captured_frames=finish-begin, output_peak=peak, output_rms=rms,
                  original_returncode=original['returncode'])
    (directory/f'audio-acceptance-{start}-{end}.json').write_text(
        json.dumps(report, indent=2)+'\n', newline='\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--start', type=int, required=True)
    parser.add_argument('--end', type=int)
    args = parser.parse_args()
    report = check(args.directory, args.start, args.end)
    print(json.dumps(report))
    return int(report['status'] != 'passed')


if __name__ == '__main__':
    raise SystemExit(main())
