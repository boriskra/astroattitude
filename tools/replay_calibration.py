#!/usr/bin/env python3
"""Local-only retrospective RAW replay. Never acquires images or modifies inputs.

prepare seals whole-block allocation and hashes BEFORE extraction; run consumes that
protocol into exclusive result files. rawpy/numpy are optional replay adapters only.
"""
import argparse
import datetime as dt
import hashlib
import json
import math
import shutil
from collections import Counter
from dataclasses import asdict
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'bindings/python'))
from astroattitude import *
from astroattitude.workflow import CalibrationSession,extract_stars
from astroattitude import _wire

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1048576),b''):h.update(chunk)
    return h.hexdigest()
def obj(path):return json.loads(path.read_text())
def save(path,value):
    with path.open('x') as f:json.dump(value,f,indent=2,allow_nan=False);f.write('\n')
def stamp(text):return dt.datetime.fromisoformat(text.replace('Z','+00:00')).timestamp()
def child(root,name):
    p=(root/name).resolve()
    if not p.is_relative_to(root.resolve()):raise ValueError('Path escapes input session')
    return p
def instant(t):
    sec=math.floor(t);ns=round((t-sec)*1e9)
    return UtcInstant(sec+ns//1000000000,ns%1000000000)
def prepare(a):
    root=a.session.resolve();a.output.mkdir(parents=True,exist_ok=False)
    shutil.copyfile(__file__,a.output/'runner-used.py');shutil.copyfile(a.native,a.output/('native-used'+a.native.suffix))
    journal=[json.loads(l) for l in (root/'block-journal.jsonl').read_text().splitlines() if l.strip()]
    declarations={}
    for entry in journal:
        if entry.get('role')=='scientific' and entry.get('label'):declarations.setdefault(entry['label'],entry)
    for role in root.glob('fixed-*/scientific-role.json'):declarations.setdefault(role.parent.name,{'label':role.parent.name})
    paths=['SESSION.md','control.json','fixed-view-plan.json','block-journal.jsonl']
    hashes={name:digest(root/name) for name in paths}
    blocks=[];previous=None
    for label,decl in sorted(declarations.items()):
        folder=child(root,label);rp=folder/'scientific-role.json';record={'label':label,'declaration':decl,'frames':[],'failures':[]}
        try:
            role=obj(rp);receipt=obj(folder/'receipt.json');raw=child(root,receipt['path']);manifest_path=raw/'night_photos.json';manifest=obj(manifest_path);validation=obj(folder/'validation.json')
            if digest(manifest_path)!=role['manifest_sha256'] or digest(manifest_path)!=validation['manifest_sha256']:raise ValueError('Manifest hash mismatch')
            if role['plan_sha256']!=hashes['fixed-view-plan.json']:raise ValueError('Plan hash mismatch')
            record.update(role=role['role'],pose_id=role['pose_id'],start=manifest['start_utc'],end=manifest['stop_utc'],raw=str(raw.relative_to(root)))
            current=stamp(record['start']);record['start_gap_seconds']=None if previous is None else current-previous;previous=current
            record['allocation']='development' if role['role']=='training' else 'evaluation'
            record['planned_count']=int(manifest['plan']['frame_count_limit'])
            record['configuration']={k:manifest['camera_configuration'][k] for k in ('camera_unique_id','physical_lens','raw_kind','raw_pixel_format')}
            record['configuration'].update(focus=manifest['plan']['focus_positions'],exposure=manifest['plan']['requested_exposure_seconds'],iso=manifest['plan']['requested_iso'])
            actual=[]
            for photo in manifest['photos']:
                state={k:photo.get(k) for k in ('cameraUniqueID','lens','rawKind','rawPixelFormat','lensPosition','exposureDurationSeconds','iso','width','height')}
                state['zoom_factor']=photo.get('pre_capture_camera_state',{}).get('zoom_factor')
                if any(v is None for v in state.values()):record['failures'].append('missing_actual_configuration_metadata')
                for flag in ('settings_readback_qualified','focus_capture_clock_guard_qualified','unique_exposure_spacing_qualified'):
                    if photo.get(flag) is not True:record['failures'].append('unqualified_capture_setting_or_clock:'+flag)
                actual.append(state)
            unique={json.dumps(v,sort_keys=True) for v in actual}
            if len(unique)!=1:record['failures'].append('configuration_changed_within_block_or_no_frames')
            record['configuration']['actual_readbacks']=json.loads(next(iter(unique))) if len(unique)==1 else None

            for name in ('scientific-role.json','receipt.json','validation.json','visual-review.json','identify-used.py','focus-metrics.json','visual-mask-trial/protocol.json','visual-mask-trial/focus-metrics.json'):
                p=folder/name
                if p.exists():hashes[str(p.relative_to(root))]=digest(p)
            hashes[str(manifest_path.relative_to(root))]=digest(manifest_path)
            sensors=raw/'sensors.jsonl'
            if sensors.exists():hashes[str(sensors.relative_to(root))]=digest(sensors)
            for i,photo in enumerate(manifest['photos'],1):
                frame={'ordinal':i,'file':photo['file'],'status':'verified','photo':photo}
                try:
                    path=child(raw,photo['file']);v=next(v for v in validation['frames'] if v['ordinal']==i)
                    if path.is_symlink() or path.stat().st_size!=photo['bytes'] or digest(path)!=v['dng_sha256']:raise ValueError('RAW hash/size mismatch')
                    frame.update(dng_sha256=v['dng_sha256'],bayer_sha256=v['bayer_sha256'])
                except Exception as e:frame.update(status='verification_failed',error=str(e))
                record['frames'].append(frame)
            if receipt['validation_returncode']!=0 or validation['status']!='PASS':record['failures'].append('historical_media_failure')
            if len(record['frames'])!=record['planned_count']:record['failures'].append('missing_scheduled_frames')
        except Exception as e:record['failures'].append(str(e));record.setdefault('allocation','not_processed');record.setdefault('planned_count',decl.get('count',12))
        blocks.append(record)
    options=ExtractionOptions(5,3,256,128,60,16777216,536870912,2000000000)
    protocol={'schema':'astroattitude.retrospective-replay/1','created_utc':dt.datetime.now(dt.timezone.utc).isoformat(),'input_root':str(root),'hashes':hashes,'blocks':blocks,'extraction_options':_wire(options),'selected_blocks':a.blocks or [b['label'] for b in blocks],
     'metrics':['scheduled/verified/failed frames','compact detections and reasons','original usable sources recovered within 3 native pixels (not truth)','native catalog association outcomes','4x3 source coverage','separate visual-mask trial'],
     'allocation_policy':'Whole historical training blocks development; all other blocks evaluation. All results retrospective, including held-out blocks. Historical roles immutable. No reserve is validation.',
     'variants':['automatic','historical_visual_mask'] if a.visual_masks else ['automatic'],
     'association_policy':{'cap':64,'order':'flux_descending_id_tie','maximum_match_error_rad':.002,'maximum_fit_rms_rad':.001},
     'physical_accuracy_qualified':False,'absolute_accuracy':'unqualified','conditional_precision':'not estimated; noise model absent','hash_of_runner':digest(Path(__file__)),
     'native_sha256':digest(a.native),'catalog_sha256':digest(a.catalog) if a.catalog else None,'astrometry_sha256':digest(a.astrometry) if a.astrometry else None,'context_sha256':digest(a.context) if a.context else None}
    if any(label not in {b['label'] for b in blocks} for label in protocol['selected_blocks']):raise ValueError('Unknown selected block')
    save(a.output/'protocol.json',protocol);print(f"Sealed {len(blocks)} blocks, {sum(b['planned_count'] for b in blocks)} scheduled frames; all retrospective",flush=True)

def explicit_mask_rectangles(review, shape):
    """Validate native-pixel caller masks without requiring an image decoder."""
    h,w=shape
    rectangles=review.get('extraction_mask_rectangles', [])
    if not isinstance(rectangles,list):raise ValueError('Mask rectangles must be a list')
    for box in rectangles:
        if not isinstance(box,list) or len(box)!=4 or any(type(v) is not int for v in box):
            raise ValueError('Mask rectangle must contain four integers')
        x0,y0,x1,y1=box
        if not (0<=x0<x1<=w and 0<=y0<y1<=h):raise ValueError('Mask rectangle outside image')
    return rectangles

def mask_from_review(review,ordinal,name,shape):
    import numpy as np
    h,w=shape;mask=np.zeros(shape,dtype=np.uint8)
    for x0,y0,x1,y1 in explicit_mask_rectangles(review,shape):
        mask[y0:y1,x0:x1]=1
    for entry in review.get('scientific_exclusions',[]):
        if isinstance(entry,dict) and entry.get('file')==name:mask[:]=1
    for key in ('scientific_source_exclusions','transient_regions','visual_source_cautions'):
        for entry in review.get(key,[]):
            if entry.get('file',name)!=name or entry.get('frame',ordinal)!=ordinal:continue
            box=entry.get('native_box',entry.get('native_xy_box'))
            if box:
                x0,y0,x1,y1=map(int,box);mask[max(0,y0):min(h,y1+1),max(0,x0):min(w,x1+1)]=1
    return mask

def run(a):
    import numpy as np
    import rawpy
    protocol=obj(a.output/'protocol.json');root=Path(protocol['input_root'])
    if digest(a.native)!=protocol['native_sha256'] or digest(Path(__file__))!=protocol['hash_of_runner']:raise ValueError('Runner/native changed after protocol')
    for name,expected in protocol['hashes'].items():
        if digest(child(root,name))!=expected:raise ValueError('Historical evidence changed: '+name)
    for key,path in [('catalog',a.catalog),('astrometry',a.astrometry),('context',a.context)]:
        if (digest(path) if path else None)!=protocol[key+'_sha256']:raise ValueError('Resource/context changed')
    results=a.output/'results';results.mkdir(exist_ok=False)
    lib=NativeLibrary(a.native);options=decode(ExtractionOptions,protocol['extraction_options']);sessions={};session_errors={};summary=[];seen=set()
    ctx=decode(EarthContext,obj(a.context)) if a.context else None
    fit_blockers=[]
    for block in protocol['blocks']:
        label=block['label'];out=results/label;out.mkdir();rows=[]
        if label not in protocol['selected_blocks'] or not block['frames'] or 'configuration' not in block or block['failures']:
            fit_blockers.append(label+': unavailable or deliberately unprocessed scheduled block')
        baseline=obj(root/label/'focus-metrics.json').get('rows',[]) if (root/label/'focus-metrics.json').exists() else []
        trial=obj(root/label/'visual-mask-trial/focus-metrics.json').get('rows',[]) if (root/label/'visual-mask-trial/focus-metrics.json').exists() else []
        review=obj(root/label/'visual-review.json') if (root/label/'visual-review.json').exists() else {}
        for frame in block['frames']:
            row={'file':frame['file'],'ordinal':frame['ordinal'],'status':frame['status'],'variants':{}}
            for name,data in [('original',baseline),('historical_visual_trial',trial)]:
                match=next((r for r in data if r['file']==frame['file']),None);row[name]={'catalog_matches':match.get('catalog_matches',0),'usable_sources':sum(bool(s.get('usable')) for s in match.get('stars',[]))} if match else None
            if label not in protocol['selected_blocks']:row['status']='not_selected_by_protocol';rows.append(row);continue
            try:
                if frame['status']!='verified' or block['failures']:raise ValueError('Failed input evidence: '+str(block['failures']))
                path=child(root/ block['raw'],frame['file'])
                if digest(path)!=frame['dng_sha256']:raise ValueError('RAW changed after protocol')
                with rawpy.imread(str(path)) as raw:
                    pixels=raw.raw_image_visible.copy();colors=raw.raw_colors_visible[:2,:2].copy();desc=raw.color_desc.decode();black=[raw.black_level_per_channel[int(c)] for c in colors.ravel()];white=raw.camera_white_level_per_channel
                    levels=[white[int(c)] if white and white[int(c)] else raw.white_level for c in colors.ravel()]
                bayer=hashlib.sha256(f'{pixels.dtype.str}:{pixels.shape}'.encode()+pixels.tobytes(order='C')).hexdigest()
                if bayer!=frame['bayer_sha256']:raise ValueError('Decoded Bayer hash mismatch')
                if bayer in seen:raise ValueError('Repeated decoded payload')
                seen.add(bayer);h,w=pixels.shape;layout=''.join(desc[int(c)] for c in colors.ravel()).lower();metadata=ExtractionMetadata(w,h,w,SampleLayout(layout),black,levels,options)
                native_pixels=np.asarray(pixels,dtype=np.float32).ravel();photo=frame['photo'];clock=photo['photoClock'];t=(clock['pairedUTCBeforeSeconds']+clock['pairedUTCAfterSeconds'])/2+clock['captureMasterClockSeconds']-clock['pairedMasterClockSeconds']+photo['exposureDurationSeconds']/2
                for variant in protocol['variants']:
                    mask=mask_from_review(review,frame['ordinal'],frame['file'],pixels.shape) if variant=='historical_visual_mask' else None
                    r=extract_stars(lib,native_pixels,metadata,None if mask is None else mask.ravel());data=_wire(r)
                    original=next((v for v in baseline if v['file']==frame['file']),{}).get('stars',[])
                    known=[s for s in original if s.get('usable')];recovered=sum(any(math.hypot(s.pixel.x-k['xy'][0],s.pixel.y-k['xy'][1])<=3 for s in r.stars) for k in known)
                    coverage=[0]*12
                    for s in r.stars:coverage[min(2,int(s.pixel.y*3/h))*4+min(3,int(s.pixel.x*4/w))]+=1
                    result={'detections':len(r.stars),'recovered_original':recovered,'original_usable':len(known),'coverage':coverage,'reasons':dict(Counter(d.reason for d in r.sources)),'association':'not_run_resources_or_context_absent','new_sources':'unverified until native association plus independent patch review'}
                    # Keep configuration and pose separate; reserve never becomes validation.
                    config=block['configuration'];key=hashlib.sha256(json.dumps([block['pose_id'],config,w,h,variant],sort_keys=True).encode()).hexdigest()
                    if a.catalog and a.astrometry and ctx:
                        if key not in sessions and key not in session_errors:
                            fov=photo['pre_capture_camera_state']['field_of_view_degrees'];f=w/(2*math.tan(math.radians(fov)/2))
                            cfg=CalibrationConfiguration(key,key,config['camera_unique_id'],'visible-integer-centers',str(config['focus'][0]),w,h,PinholeBrownModel(f,f,(w-1)/2,(h-1)/2,0,0,0,0,0),SolveOptions(AcceptanceCriteria(6,protocol['association_policy']['maximum_match_error_rad'],protocol['association_policy']['maximum_fit_rms_rad'],None),SolveLimits(50000,2000000,67108864)),2000,256,60,100000000,268435456,0)
                            try:sessions[key]=CalibrationSession(lib,a.catalog.read_bytes(),a.astrometry.read_bytes(),cfg)
                            except Exception as e:session_errors[key]=str(e)
                        if key in session_errors:result['association']='session_error: '+session_errors[key]
                        else:
                            role=CaptureRole.reserve if block['role']=='reserve_pending_freeze' else CaptureRole(block['role'])
                            input_frame=CalibrationFrame(label+'/'+frame['file'],block['pose_id'],label,key,bayer,r.policy_id,'capture_clock_bridge_midpoint_assumption',role,ExposureTime(instant(t),photo['exposureDurationSeconds'],None),ctx,sorted(r.stars,key=lambda star:(-star.flux,star.id))[:protocol['association_policy']['cap']])
                            try:
                                receipt=sessions[key].add_frame(input_frame);result['association']=_wire(receipt)
                                evidence=[]
                                for match in receipt.matches:
                                    star=next(s for s in input_frame.stars if s.id==match.detection_id)
                                    nearby=[k for k in known if math.hypot(star.pixel.x-k['xy'][0],star.pixel.y-k['xy'][1])<=3]
                                    agrees=any(match.catalog_star_id.rsplit(':',1)[-1]==str(k['HIP']) for k in nearby)
                                    evidence.append({'detection_id':str(match.detection_id),'catalog_star_id':match.catalog_star_id,'status':'agrees_with_historical_id' if agrees else 'contradicts_historical_id' if nearby else 'requires_independent_patch_and_id_review'})
                                result['source_corroboration']=evidence
                            except Exception as e:result['association']='ingestion_error: '+str(e)
                    save(out/(frame['file']+'.'+variant+'.json'),{'metadata':_wire(metadata),'mask_sha256':hashlib.sha256(mask.tobytes()).hexdigest() if mask is not None else None,'extraction':data,'comparison':result})
                    row['variants'][variant]=result
                row['status']='processed'
            except Exception as e:row['status']='failed';row['error']=str(e)
            rows.append(row)
            save(out/(frame['file']+'.receipt.json'),row)
        # Every expected ordinal and block remains in the report, even missing media.
        while len(rows)<block['planned_count']:rows.append({'ordinal':len(rows)+1,'status':'missing_capture','variants':{}})
        for key,session in sessions.items():
            expected_keys=set()
            if block['frames'] and 'configuration' in block and all(k in block['frames'][0]['photo'] for k in ('width','height')):
                photo=block['frames'][0]['photo']
                expected_keys={hashlib.sha256(json.dumps([block['pose_id'],block['configuration'],photo['width'],photo['height'],variant],sort_keys=True).encode()).hexdigest() for variant in protocol['variants']}
            if key not in expected_keys:continue
            relevant=[r for r in rows if any(isinstance(v.get('association'),dict) and v['association']['frame_id']==label+'/'+r.get('file','') for v in r['variants'].values())]
            # Sessions for each variant receive only that variant; frame IDs in the journal are authoritative.
            journal=json.loads(session.journal());ids=[v['value']['id'] for v in journal['actions'] if v['op']=='frame' and v['value']['block_id']==label]
            if key in expected_keys:
                planned=[label+'/'+f['file'] for f in block['frames']]
                planned += [label+'/missing-'+str(i+1) for i in range(len(planned),block['planned_count'])]
                missing=[p for p in planned if p not in ids]
                role=CaptureRole.reserve if block['role']=='reserve_pending_freeze' else CaptureRole(block['role'])
                try:session.finish_block(BlockCompletion(label,block['pose_id'],role,instant(stamp(block['start'])),instant(stamp(block['end'])),planned,missing,['ingestion_failed']*len(missing)))
                except Exception as e:
                    fit_blockers.append(label+': native block completion failed')
                    save(out/(key+'.block-error.json'),{'error':str(e)})
        summary.append({'label':label,'role':block.get('role'),'allocation':block.get('allocation'),'start_gap_seconds':block.get('start_gap_seconds'),'failures':block['failures'],'frames':rows})
        save(out/'block.json',summary[-1]);print(label,dict(Counter(r['status'] for r in rows)),flush=True)
    for key,session in sessions.items():
        try:
            save(results/(key+'.progress.json'),_wire(session.progress()))
            try:
                if fit_blockers:raise ValueError('Unaccounted scheduled blocks prevent fitting: '+str(fit_blockers))
                with session.fit() as candidate:save(results/(key+'.diagnostic-candidate.json'),_wire(candidate.data))
            except Exception as e:save(results/(key+'.fit-failure.json'),{'error':str(e),'models':_wire(session.model_diagnostics())})
            (results/(key+'.journal.json')).write_text(session.journal())
        finally:session.close()
    save(a.output/'report.json',{'blocks':summary,'fit_blockers':fit_blockers,'session_errors':session_errors,'label':'retrospective','physical_accuracy_qualified':False,'absolute_accuracy':'unqualified','qualification':'no fresh validation; recovery does not establish north accuracy'})

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['prepare','run']);p.add_argument('--session',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--native',type=Path,required=True);p.add_argument('--catalog',type=Path);p.add_argument('--astrometry',type=Path);p.add_argument('--context',type=Path);p.add_argument('--blocks',nargs='*');p.add_argument('--visual-masks',action='store_true');a=p.parse_args()
    if a.action=='prepare':
        if not a.session:p.error('--session required')
        prepare(a)
    else:run(a)
if __name__=='__main__':main()
