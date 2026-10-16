#!/usr/bin/env python3
"""Report a passive original-audio API journal; never certify family acceptance."""
import argparse
from collections import Counter
import json
from pathlib import Path


def _read_journal(path, live=False):
    data=path.read_bytes()
    if data and not data.endswith(b'\n'):
        if not live:raise ValueError('incomplete journal record')
        data=data[:data.rfind(b'\n')+1]
    rows=[json.loads(line) for line in data.splitlines()]
    if not rows or rows[0].get('schema')!='xboxlle.original-audio-calls.v1':
        raise ValueError('missing original audio journal schema')
    return rows


def summarize(path, live=False):
    return _summarize_rows(_read_journal(path, live))


def _summarize_rows(rows):
    active={};seen=set();counts=Counter();played=set();stopped=set();restarted=set()
    releases=0;fences=pcm=spdif=ticks=0;footer=None;records=0;had_play=had_stop=False
    input_success=input_failed=input_start=0;input_first=None;input_first_start=None
    for row in rows[1:]:
        if footer is not None:raise ValueError('records after journal footer')
        if row.get('footer'):
            footer=row
            if row['records']!=records or row['calls']!=len(seen) or row['active_calls']!=len(active):
                raise ValueError('journal footer disagrees with observed calls')
            continue
        records+=1
        identity=row['id'];name=row['name']
        if row['ticks']<ticks:raise ValueError('device clock moved backwards in call journal')
        ticks=row['ticks'];fences=max(fences,row['fences'])
        pcm=max(pcm,row['pcm_words']);spdif=max(spdif,row['spdif_words'])
        if row['phase']=='enter':
            if identity in seen:raise ValueError('reused call identity')
            seen.add(identity);active[identity]=row
            continue
        if row['phase']!='return' or identity not in active:raise ValueError('unmatched call return')
        enter=active.pop(identity)
        if any(enter[k]!=row[k] for k in ('entry','name','address','thread')):
            raise ValueError('return identity or thread changed')
        for key in ('caller','entry_esp'):
            if key in enter or key in row:
                if enter.get(key)!=row.get(key):raise ValueError('return caller or entry stack changed')
        counts[name]+=1
        if name=='XInputGetState':
            if row['eax']:
                input_failed+=1
            else:
                outputs=row.get('outputs',[])
                if len(outputs)!=1 or outputs[0].get('arg')!=1:
                    raise ValueError('successful input read lacks its actual state')
                pad=bytes.fromhex(outputs[0].get('gamepad',''))
                if len(pad)!=18 or not 0<=outputs[0].get('packet',-1)<=0xffffffff:
                    raise ValueError('invalid original input state layout')
                input_success+=1
                if input_first is None:input_first=row['ticks']
                if int.from_bytes(pad[:2],'little')&0x10:
                    input_start+=1
                    if input_first_start is None:input_first_start=row['ticks']
        obj=enter['args'][0] if enter['args'] else None
        if row['eax']==0 and name=='IDirectSoundBuffer_Play':
            had_play=True
            played.add(obj)
            if obj in stopped:restarted.add(obj)
        elif row['eax']==0 and name=='IDirectSoundBuffer_Stop':
            had_stop=True
            if obj in played:stopped.add(obj)
        elif row['eax']==0 and name=='IDirectSoundBuffer_Release':
            releases+=obj in played;played.discard(obj);stopped.discard(obj)
    milestones=dict(successful_play=had_play,
        successful_stop=had_stop,same_buffer_restart=bool(restarted),
        final_release_of_played_buffer=bool(releases),device_fence_published=bool(fences),
        both_outputs_observed=bool(pcm and spdif))
    return dict(schema='xboxlle.original-audio-call-report.v1',partial=True,
        journal_finalized=footer is not None,records=records,calls=len(seen),active_calls=len(active),
        completed_calls=dict(counts),milestones=milestones,last_device_ticks=ticks,
        fences=fences,pcm_words=pcm,spdif_words=spdif,
        input_observations=dict(successful_reads=input_success,failed_reads=input_failed,
            start_reads=input_start,first_success_ticks=input_first,first_start_ticks=input_first_start),
        lifecycle_candidate=all(milestones.values()),
        limits='API return/counter observations only. Candidate requests review; it does not prove DSP output, movie-relative timing, internal ownership or whole-family acceptance.')


def retained_buffer_checkpoint(path, post_restart_frames=96000):
    """Request review after original Stop/reuse and continued stereo output.

    Blinx retains its title-music buffer through normal Stop. Final Release and
    the independently qualified device-fence component are not capture triggers.
    This observes real API results; it neither supplies them nor certifies audio.
    """
    if post_restart_frames <= 0:
        raise ValueError('post-restart observation must contain output frames')
    if not path.exists() or not path.stat().st_size:
        return None
    rows = _read_journal(path, live=True)
    report = _summarize_rows(rows)  # Validate the exact immutable prefix used below.
    active = {}; playing = set(); stopped = {}; candidate = None
    pcm = spdif = 0
    for row in rows[1:]:
        if row.get('footer'):
            continue
        if row['pcm_words'] < pcm or row['spdif_words'] < spdif:
            raise ValueError('output counter moved backwards in lifecycle observation')
        pcm, spdif = row['pcm_words'], row['spdif_words']
        if row['phase'] == 'enter':
            active[row['id']] = row
            continue
        enter = active.pop(row['id'])
        obj = enter['args'][0] if enter['args'] else None
        if row['eax'] == 0:
            if row['name'] == 'IDirectSoundBuffer_Play':
                playing.add(obj)
                if obj in stopped:
                    candidate = dict(object=obj, stop=stopped.pop(obj), restart=row)
            elif row['name'] == 'IDirectSoundBuffer_Stop':
                if obj in playing:
                    stopped[obj] = row
                playing.discard(obj)
                if candidate and candidate['object'] == obj:
                    candidate = None
            elif row['name'] == 'IDirectSoundBuffer_Release':
                playing.discard(obj); stopped.pop(obj, None)
                if candidate and candidate['object'] == obj:
                    candidate = None
        if candidate:
            restart = candidate['restart']
            if (pcm >= restart['pcm_words'] + 2*post_restart_frames and
                    spdif >= restart['spdif_words'] + 2*post_restart_frames):
                return dict(scope='Original same-buffer Stop/reuse with continued analog/digital output; request review only',
                    **candidate, observed_through=row, post_restart_frames=post_restart_frames,
                    report=report, lifecycle_acceptance=False)
    return None


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal',type=Path)
    parser.add_argument('--live',action='store_true',help='Allow an unfinished final record')
    args=parser.parse_args()
    print(json.dumps(summarize(args.journal,args.live),indent=2))


if __name__=='__main__':main()
