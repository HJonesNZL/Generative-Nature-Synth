"""Bounded retrospective source qualification. Raw responses live only in temp files.
Compile normalizer_probe.cpp against current native libraries, then:
python3 -B evidence/worldwide/qualify_24h.py --normalizer /tmp/atlas-world-normalizer --end 2026-11-07T04:00:00Z --output /tmp/receipt.json
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from urllib.parse import urlencode

def dt(s): return datetime.fromisoformat(s.replace('Z','+00:00')).astimezone(timezone.utc)
def stamp(x): return x.isoformat(timespec='seconds').replace('+00:00','Z')
def area(lat,lon):
    # Disjoint representative rectangles, never inferred from provider labels.
    if lat < 0 and (lon >= 110 or lon < -110): return 'Oceania-Pacific'
    if -90 <= lon < -25: return 'North-America-area' if lat >= 12 else 'South-America-area'
    if lon < -90: return 'North-America-area' if lat >= 0 else 'Other-Pacific'
    if -25 <= lon < 55 and -60 <= lat < 37: return 'Africa-area'
    if -25 <= lon < 60 and lat >= 37: return 'Europe-area'
    if 25 <= lon <= 180 and lat >= 0: return 'Asia-area'
    return 'Other-ocean-area'
def quantiles(values):
    x=sorted(values)
    return {} if not x else dict(min=x[0],p50=x[(len(x)-1)//2],p95=x[int((len(x)-1)*.95)],max=x[-1])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--normalizer',type=Path,required=True)
    p.add_argument('--end',required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(); end=dt(a.end); start=end-timedelta(hours=24)
    assert end.minute==end.second==end.microsecond==0 and end < datetime.now(timezone.utc)
    fetched=datetime.now(timezone.utc); requests=[]; bins=[]; accepted={}; raw_taxa={}; last_request=0
    with tempfile.TemporaryDirectory(prefix='atlas-world-24h-') as temp:
        folder=Path(temp)
        def fetch(url,name):
            nonlocal last_request
            delay=2-(time.monotonic()-last_request)
            if delay>0: time.sleep(delay)
            path=folder/name; last_request=time.monotonic()
            result=subprocess.run(['curl','--silent','--show-error','--fail','--max-time','20','--max-filesize','4194304',url,'-o',str(path)],capture_output=True,text=True)
            if result.returncode: raise RuntimeError('bounded_http_failure_'+str(result.returncode))
            body=path.read_bytes(); assert len(body)<=4194304
            requests.append(dict(url=url if 'subjects?' not in url else 'https://ermap-server.pamdas.org/nzsharkman/api/v1.0/subjects?[public group redacted]',retrieved_utc=stamp(datetime.now(timezone.utc)),bytes=len(body),sha256=hashlib.sha256(body).hexdigest()))
            return json.loads(body)
        def normalize(mode,path):
            result=subprocess.run([str(a.normalizer.resolve()),mode,str(path),stamp(datetime.now(timezone.utc)),str(folder/'probe.sqlite3')],check=True,capture_output=True,text=True)
            return [json.loads(line) for line in result.stdout.splitlines()]
        for hour in range(24):
            left=start+timedelta(hours=hour); right=left+timedelta(hours=1)
            query=dict(taxon_id=1,license='cc0,cc-by',geo='true',geoprivacy='open',taxon_geoprivacy='open',per_page=10,order='desc',order_by='observed_on',d1=stamp(left),d2=stamp(right),page=1)
            entry=dict(start_utc=stamp(left),end_utc=stamp(right),provider='inaturalist',sample_cap=10)
            try:
                raw=fetch('https://api.inaturalist.org/v1/observations?'+urlencode(query),'inat.json')
                records=normalize('inat',folder/'inat.json')
                ids={str(r['id']):r for r in raw['results']}
                kept=[]
                for record in records:
                    when=dt(record['observed_at_utc'])
                    if left<=when<right:
                        kept.append(record)
                        accepted[record['event_id']]=record
                        raw_taxa[record['event_id']]=ids[record['provider_event_id']].get('taxon',{}).get('iconic_taxon_name','unknown')
                entry.update(returned=len(raw['results']),reported_matching=raw['total_results'],normalizer_accepted=len(records),accepted_in_bin=len(kept),outside_bin=len(records)-len(kept),sample_capped=raw['total_results']>10)
            except Exception as error: entry.update(error=type(error).__name__,accepted_in_bin=0)
            bins.append(entry)
            print('hour',hour,'accepted',entry['accepted_in_bin'],flush=True)
        shark=dict(provider='nzsharks',accepted_in_window=0)
        try:
            bootstrap=fetch('https://map.sustainableoceansociety.co.nz/public/bootstrap.json','bootstrap.json')
            assert bootstrap['configUrl']=='https://map.sustainableoceansociety.co.nz/public/configs/config.json'
            config=fetch(bootstrap['configUrl'],'config.json')
            assert config['server']=='ermap-server.pamdas.org' and config['public_name']=='nzsharkman'
            raw=fetch('https://ermap-server.pamdas.org/nzsharkman/api/v1.0/subjects?'+urlencode(dict(subject_group=config['subject_group'])),'subjects.json')
            records=normalize('sharks',folder)
            for record in records:
                if start<=dt(record['observed_at_utc'])<end:
                    accepted[record['event_id']]=record; raw_taxa[record['event_id']]='Chondrichthyes'; shark['accepted_in_window']+=1
            shark.update(normalizer_accepted=len(records),outside_window=len(records)-shark['accepted_in_window'],latest_snapshot_only=True)
        except Exception as error: shark['error']=type(error).__name__
    providers=Counter(); regions=Counter(); taxa=Counter(); species=set(); by_provider_region=defaultdict(Counter); ages=[]
    for event_id,record in accepted.items():
        provider=record['source_id']; region=area(record['latitude'],record['longitude'])
        providers[provider]+=1; regions[region]+=1; taxa[raw_taxa[event_id]]+=1; species.add(record.get('scientific_name'))
        by_provider_region[provider][region]+=1
        ages.append(round((fetched-dt(record['observed_at_utc'])).total_seconds()/3600,3))
    receipt=dict(kind='retrospective_completed_24h_observation_window_sample',window_start_utc=stamp(start),window_end_utc=stamp(end),qualification_started_utc=stamp(fetched),qualification_completed_utc=stamp(datetime.now(timezone.utc)),continuous_uptime_claim=False,counts=dict(unique_accepted=len(accepted),providers=dict(providers),regions=dict(regions),taxa=dict(taxa),distinct_scientific_names=len(species-{None}),provider_region={k:dict(v) for k,v in by_provider_region.items()}),observation_age_hours_at_start=quantiles(ages),hourly_bins=bins,nzsharks=shark,empty_or_failed_hour_bins=[b['start_utc'] for b in bins if not b['accepted_in_bin']],requests=requests,policy=dict(inaturalist='24 sequential hourly observed-time queries, first 10 rows by observed_on descending, no pagination; exact production C++ normalizer then strict half-open UTC observation-bin filter',sharks='One current latest-position snapshot, exact production C++ Collector, strict same observation-window filter; historical hourly recovery unavailable',cadence='At least two seconds between request starts, 20-second timeout, 4MiB per response; no retries',privacy='No raw sightings, coordinates, observer identifiers or event IDs persisted in this receipt; only aggregates and response hashes',geometry='Disjoint coarse rectangles from area() in checked script; area names approximate geography, not political continent assignment',coverage='Samples prove actual accepted observations, not all occurrences, complete hourly regional coverage, provider reporting cadence, rights for BirdWeather, or 24h collector uptime'),acceptance=dict(at_least_three_regions=len(regions)>=3,at_least_two_providers=len(providers)>=2,nonbird=any(k not in ['Aves','unknown'] for k in taxa)))
    root=Path(__file__).resolve().parents[2]
    receipt['normalizer_source_sha256']={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ['native/src/inaturalist.cpp','native/src/collection.cpp','native/src/event_validation.cpp','evidence/worldwide/normalizer_probe.cpp']}
    receipt['normalizer_binary_sha256']=hashlib.sha256(a.normalizer.read_bytes()).hexdigest()
    receipt['request_count']=len(requests)
    receipt['qualifying_sightings_after_native_normalizer']=sum(b.get('normalizer_accepted',0) for b in bins)
    receipt['sightings_excluded_by_strict_hour_window']=sum(b.get('outside_bin',0) for b in bins)
    a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_text(json.dumps(receipt,indent=2)+'\n'); print(json.dumps(receipt['counts']),flush=True)
if __name__=='__main__': main()
