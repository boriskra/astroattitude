#!/usr/bin/env python3
"""Offline source converters. No downloading, rights approval, or implicit validity."""
import argparse, hashlib, json, math, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def canonical(value): return (json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True,allow_nan=False)+'\n').encode()
def sha(value): return hashlib.sha256(value).hexdigest()
def read_local(path,limit=128*1024*1024):
    p=Path(path)
    if p.stat().st_size>limit: raise ValueError('Source exceeds converter limit')
    return p.read_bytes()
def number(text):
    v=float(text)
    if not math.isfinite(v): raise ValueError('Nonfinite source field')
    return v

def hipparcos(raw,helper):
    accepted=[];excluded=[];inputs=[]
    for index,line in enumerate(raw.decode('ascii').splitlines(),1):
        f=line.split('|');sid=f[1].strip() if len(f)>1 else 'line-'+str(index)
        try:
            if len(f)<59: raise ValueError('incomplete_row')
            if not sid.isdigit() or int(sid) in (54738,114110,114176): raise ValueError('excluded_id')
            if any(f[k].strip() for k in (2,6,10,59)): raise ValueError('quality_flag')
            ra,dec,px,pmr,pmd,mag=[number(f[k]) for k in (8,9,11,12,13,5)]
            if px<=0 or mag>6.5: raise ValueError('parallax_or_magnitude')
            ra*=math.pi/180;dec*=math.pi/180
            if abs(dec)>=math.pi/2: raise ValueError('polar_coordinate')
            scale=math.pi/(180*3600*1000)
            inputs.append((ra,dec,pmr*scale/math.cos(dec),pmd*scale,px/1000,0,2448349.0625))
            accepted.append((sid,mag))
        except (ValueError,IndexError) as e: excluded.append({'id':sid,'reason':str(e)})
    proc=subprocess.run([str(helper)],input=''.join(' '.join(format(v,'.17g') for v in row)+'\n' for row in inputs),text=True,capture_output=True,check=True)
    lines=proc.stdout.splitlines()
    if len(lines)!=len(inputs): raise ValueError('Propagation response count mismatch')
    stars=[]
    for (sid,mag),line in zip(accepted,lines):
        values=line.split();status=int(values[0]);v=list(map(number,values[1:]))
        if status!=0: excluded.append({'id':sid,'reason':'erfa_pmsafe_status_'+str(status)});continue
        if len(v)!=6: raise ValueError('Propagation response shape')
        ra,dec,pmr,pmd,px,rv=v
        if not (0<=ra<2*math.pi and abs(dec)<math.pi/2 and abs(pmr)<=.1 and abs(pmd)<=.1 and 0<=px<=10 and abs(rv)<=1000 and -30<=mag<=40):
            excluded.append({'id':sid,'reason':'output_domain'});continue
        stars.append(dict(id=sid,ra_rad=ra,dec_rad=dec,pm_ra_rad_per_jyear=pmr,pm_dec_rad_per_jyear=pmd,parallax_arcsec=px,radial_velocity_km_s=rv,magnitude=mag,motion_status='measured',parallax_status='measured',radial_velocity_status='assumed'))
    stars.sort(key=lambda s:s['id'])
    if not 6<=len(stars)<=10000 or len({s['id'] for s in stars})!=len(stars): raise ValueError('Invalid survivor count/duplicate IDs')
    return stars,excluded

def finals(raw,start,end):
    rows=[];records=[]
    for line in raw.decode('ascii').splitlines():
        try:
            mjd=number(line[7:15]);t=int((mjd-40587)*86400)
            if mjd!=int(mjd): raise ValueError('Fractional MJD')
            b=[line[134:144],line[144:154],line[154:165]]
            flags=[line[16:17],line[57:58]]
            if all(v.strip() for v in b): x,y,d=map(number,b);choice='B';status='observed'
            else:
                if any(f not in ('I','P') for f in flags): continue
                x,y,d=map(number,[line[18:27],line[37:46],line[58:68]]);choice='A';status='predicted' if 'P' in flags else 'observed'
            if not (abs(d)<=1 and abs(x)*math.pi/648000<=1e-4 and abs(y)*math.pi/648000<=1e-4): raise ValueError('EOP range')
            rows.append(dict(utc_s=t,ut1_minus_utc_s=d,xp_rad=x*math.pi/648000,yp_rad=y*math.pi/648000,status=status));records.append(dict(utc_s=t,choice=choice,flags=flags))
        except ValueError:
            if not line[18:27].strip() and not line[58:68].strip(): continue
            raise
    if any(a['utc_s']>=b['utc_s'] for a,b in zip(rows,rows[1:])): raise ValueError('EOP ordering')
    before=[i for i,r in enumerate(rows) if r['utc_s']<=start];after=[i for i,r in enumerate(rows) if r['utc_s']>=end]
    if not before or not after: raise ValueError('EOP does not bracket requested validity')
    lo,hi=before[-1],after[0];rows=rows[lo:hi+1]
    if any(b['utc_s']-a['utc_s']>172800 for a,b in zip(rows,rows[1:])): raise ValueError('EOP gaps')
    return rows,records[lo:hi+1]

def leaps(raw,start,end):
    rows=[];expiry=None
    for line in raw.decode('ascii').splitlines():
        if line.startswith('#@'): expiry=int(line[2:].strip())-2208988800
        if not line.strip() or line.startswith('#'): continue
        f=line.split('#')[0].split();rows.append(dict(effective_utc_s=int(f[0])-2208988800,tai_minus_utc_s=int(f[1])))
    if expiry is None or end>expiry: raise ValueError('Leap expiry does not cover validity')
    before=[i for i,r in enumerate(rows) if r['effective_utc_s']<=start]
    if not before: raise ValueError('Missing initial leap offset')
    return [r for r in rows[before[-1]:] if r['effective_utc_s']<=end],expiry

def check_eop_leap_bracket(eop, until):
    if eop[-1]['utc_s'] >= until:
        raise ValueError('Leap announcement/expiry must cover the upper EOP interpolation sample exclusively')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('kind',choices=['hipparcos','astrometry']);p.add_argument('--inputs',type=Path,required=True,help='Local JSON source metadata list with path plus required provenance fields');p.add_argument('--valid-from',type=int,required=True);p.add_argument('--valid-until',type=int,required=True);p.add_argument('--announcement-valid-until',type=int,help='Explicit caller-reviewed announcement bound, not inferred');p.add_argument('--helper',type=Path,default=ROOT/'build/aa_propagate');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if not 946684800<=a.valid_from<a.valid_until<=2524608000: p.error('Validity outside supported domain')
    metadata=json.loads(read_local(a.inputs,65536));sources=[];raw={}
    if not isinstance(metadata,list) or not 1<=len(metadata)<=16: p.error('Expected 1..16 local sources')
    for entry in metadata:
        record=dict(entry);path=record.pop('path');data=read_local(a.inputs.parent/path);record['source_sha256']=sha(data)
        if set(record)!={'source_id','source_version','source_uri','source_sha256','license_id','attribution','role'}: p.error('Invalid source metadata')
        if record['role'] in raw and record['role']!='documentation': p.error('Duplicate source role')
        raw[record['role']]=data;sources.append(record)
    sources.sort(key=lambda s:s['source_id'])
    manifest=dict(converter='astroattitude-convert/1',dependencies=json.loads((ROOT/'cmake/dependencies.json').read_text()),sources=sources,valid_from_utc_s=a.valid_from,valid_until_utc_s=a.valid_until,rights_disposition='not_evaluated')
    package=dict(valid_from_utc_s=a.valid_from,valid_until_utc_s=a.valid_until)
    if a.kind=='hipparcos':
        stars,excluded=hipparcos(raw['catalog'],a.helper.resolve());manifest.update(excluded=excluded,selected=len(stars),profile='hipparcos-H8-H9-V6.5-H2-H6-H10-H59/1',source_epoch_jd=2448349.0625,destination_epoch_jd=2451545.,tt_approximates_tdb=True,radial_velocity_origin='assumed_zero_then_propagated',helper_sha256=sha(read_local(a.helper)))
        package.update(schema='astroattitude.catalog/1',namespace='hipparcos',frame='ICRS',epoch_jd_tdb=2451545.,coverage=dict(kind='declination_band',min_dec_rad=-math.pi/2,max_dec_rad=math.pi/2,limiting_magnitude=6.5),stars=stars)
    else:
        if 'announcement' not in raw or a.announcement_valid_until is None: p.error('Announcement source and reviewed validity required')
        eop,selection=finals(raw['eop'],a.valid_from,a.valid_until);offsets,expiry=leaps(raw['leaps'],a.valid_from,a.valid_until);until=min(expiry,a.announcement_valid_until)
        if a.valid_until>until: p.error('Announcement does not cover validity')
        check_eop_leap_bracket(eop,until)
        manifest.update(eop_selection=selection,leap_source_expiry=expiry,announcement_valid_until=a.announcement_valid_until,omitted=['dX','dY','LOD','subdaily_corrections'])
        package.update(schema='astroattitude.astrometry/1',model_id='erfa-2.0.1-iau2006a-tt/1',leap_valid_until_utc_s=until,leaps=offsets,eop=eop)
    manifest_bytes=canonical(manifest);package['provenance']=dict(sources=sources,conversion_id='astroattitude-convert/1',conversion_manifest_sha256=sha(manifest_bytes));package_bytes=canonical(package)
    # Exclusive creates prevent accidental replacement of application resources.
    with a.output.with_suffix('.manifest.json').open('xb') as f: f.write(manifest_bytes)
    with a.output.open('xb') as f: f.write(package_bytes)
    with a.output.with_suffix('.record.json').open('xb') as f: f.write(canonical(dict(package_sha256=sha(package_bytes),manifest_sha256=sha(manifest_bytes),rights_disposition='not_evaluated')))
if __name__=='__main__': main()
