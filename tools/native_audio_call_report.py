#!/usr/bin/env python3
"""Report a passive original-audio API journal; never certify family acceptance."""
import argparse
from collections import Counter
import json
from pathlib import Path


def summarize(path, live=False):
    data=path.read_bytes()
    if data and not data.endswith(b'\n'):
        if not live:raise ValueError('incomplete journal record')
        data=data[:data.rfind(b'\n')+1]
    rows=[json.loads(line) for line in data.splitlines()]
    if not rows or rows[0].get('schema')!='xboxlle.original-audio-calls.v1':
        raise ValueError('missing original audio journal schema')
    active={};seen=set();counts=Counter();played=set();stopped=set();restarted=set()
    releases=0;fences=pcm=spdif=ticks=0;footer=None;records=0;had_play=had_stop=False
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
        counts[name]+=1
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
        lifecycle_candidate=all(milestones.values()),
        limits='API return/counter observations only. Candidate requests review; it does not prove DSP output, movie-relative timing, internal ownership or whole-family acceptance.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal',type=Path)
    parser.add_argument('--live',action='store_true',help='Allow an unfinished final record')
    args=parser.parse_args()
    print(json.dumps(summarize(args.journal,args.live),indent=2))


if __name__=='__main__':main()
