#!/usr/bin/env python3
"""Exercise a saved game through the ordinary renderer/world, with no lab scene."""
import argparse,json,os,plistlib,re,statistics,subprocess,time
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--app',type=Path,required=True);p.add_argument('--profile',type=Path,required=True);p.add_argument('--default-profile',action='store_true');p.add_argument('--experimental-wing',action='store_true');p.add_argument('--data',type=Path,required=True);p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--case',choices=['hit','miss','external','pristine','normal','dodo','impact','graze','body-ground','body-bullets','body-tail','body-vehicles','body-curb','body-bump'],default='impact');a=p.parse_args();work=a.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
 if a.case in ['hit','external','pristine','graze'] and not a.experimental_wing:raise RuntimeError('This case measures the opt-in implicit solver; pass --experimental-wing explicitly')
 if (work/'report.json').exists() or (work/'run.log').exists():raise RuntimeError('Use a fresh evidence directory')
 app=a.app.resolve();info=plistlib.loads((app/'Contents/Info.plist').read_bytes());binary=app/'Contents/MacOS'/info['CFBundleExecutable'];capture=work/'normal.ppm'
 env=os.environ.copy();env.update(VC_REGULAR_SMOKE=str(capture),VC_REGULAR_PROFILE=str(a.profile.resolve()),VC_REGULAR_SAVE_SLOT='0',ASAN_OPTIONS='detect_leaks=0:abort_on_error=0')
 if a.experimental_wing:env['VC_EXPERIMENTAL_WING']='1'
 else:env.pop('VC_EXPERIMENTAL_WING',None)
 if a.default_profile:
  if a.profile.resolve()!=(Path.home()/'Library/Application Support/macVC-source').resolve():raise RuntimeError('Default profile must be the normal source-game profile')
  env.pop('VC_REGULAR_PROFILE',None)
 if a.case!='normal':env.update(VC_REGULAR_WING=a.case,VC_REGULAR_REPLAY=str(work/'encounter.trace'))
 else:env.pop('VC_REGULAR_WING',None)
 # The installed launcher retains normal game diagnostics in Library/Logs.
 # Copy only this launch's appended bytes into its evidence directory; never
 # count receipts left by an earlier game as assertions for the current run.
 game_log=Path.home()/'Library/Logs/VCGameplay.log' if binary.name=='vcLauncher' else None
 log_before=game_log.stat() if game_log and game_log.exists() else None
 start=time.monotonic()
 with (work/'run.log').open('w')as log:
  process=subprocess.Popen([str(binary),'-ApplePersistenceIgnoreState','YES'],cwd=a.data.resolve(),env=env,stdout=log,stderr=subprocess.STDOUT)
  try:status=process.wait(timeout=2400 if a.case=='body-bump' else 1200 if a.case=='body-curb' else 90)
  except subprocess.TimeoutExpired:process.kill();process.wait();raise RuntimeError('Ordinary game timed out; see run.log')
 if game_log and game_log.exists():
  after=game_log.stat();offset=log_before.st_size if log_before and after.st_ino==log_before.st_ino and after.st_size>=log_before.st_size else 0
  with game_log.open('rb') as source,(work/'run.log').open('ab') as destination:source.seek(offset);destination.write(source.read())
 text=(work/'run.log').read_text(errors='replace');mod=(a.profile/'vcmodernhud.log').read_text(errors='replace')
 assertions={'normal_exit':status==0,'no_memory_error':'ERROR: AddressSanitizer' not in text,'no_host_assertion':'reVC ASSERT FAILED' not in text,'mod_loaded':any(int(n)>=162 for n in re.findall(r'found table with (\d+) hooks',mod)) and 'staying vanilla' not in mod,'full_game_render':capture.exists() and capture.stat().st_size>10000,'no_native_overlap':not re.search(r'native_blocked=[1-9]',text),'no_protocol_pause':'[gameplay-wing] paused' not in text,'no_numerical_fallback':'numerical boundary handoff' not in text}
 if not a.experimental_wing:assertions['native_default_motion']='owner=coordinator' not in text
 if a.case not in ['normal','body-vehicles','body-curb','body-bump']:
  terrain=re.findall(r'terrain coverage=(PASS|FAIL) penetration=(PASS|FAIL)',text);assertions.update(terrain_collision_coverage=bool(terrain) and all(c=='PASS' for c,p in terrain),no_ground_tunneling=bool(terrain) and all(p=='PASS' for c,p in terrain))
 if binary.name=='vcLauncher':assertions['bundle_signature_intact']=subprocess.run(['codesign','--verify','--deep','--strict',str(app)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
 if a.case not in ['normal','dodo','miss','impact','body-ground','body-bullets','body-tail','body-vehicles','body-curb','body-bump']:
  proofs=re.findall(r'native-query-proof[^\n]*result=(PASS|FAIL)',text);assertions.update(owner_acquired='owner=coordinator' in text,native_queries=bool(proofs) and all(x=='PASS'for x in proofs))
 if a.case in ['hit','external','graze']:assertions.update(visible_fracture='[regular-wing-smoke] visible fracture' in text,retired_repair='retired-aircraft repair result=PASS' in text,repaired_rig='repaired-rig result=PASS' in text,damage_capture=Path(str(capture)+'.damaged.ppm').exists())
 if a.case=='graze':
  assertions.pop('visible_fracture',None);assertions.update(permanent_wing_deformation='permanent wing deformation=PASS' in text,damaged_wing_queries=bool(re.search(r'native-query-proof[^\n]*result=PASS plastic=0\.[0-9]*[1-9]',text)))
 if a.case=='external':assertions['external_force_once']='external-force handoff result=PASS' in text
 if a.case=='pristine':assertions.update(pristine_callback='pristine-callback result=PASS' in text,pristine_properties='pristine-properties result=PASS' in text)
 if a.case=='dodo':assertions.update(private_rig='[dodo-rig] prepared' in text,animated_surfaces_yoke_seat='rig surfaces yoke seat result=PASS' in text,visible_tire_support='parked tire support result=PASS' in text,taxi_braking='taxi brake result=PASS' in text,force_driven_takeoff='force driven takeoff result=PASS' in text,same_address_invalidation='same address lifecycle invalidation result=PASS' in text,no_failed_dodo_check=not re.search(r'\[dodo-smoke\][^\n]*result=FAIL',text))
 if a.case=='miss':
  assertions.update(no_fracture='[regular-wing-smoke] visible fracture' not in text,native_free_flight='owner=coordinator' not in text)
 if a.case=='body-tail':assertions.update(crash_then_tail_lifetime='crash then tail shooting retains aircraft result=PASS' in text,after_crash_exit='after real crash exit result=PASS' in text,no_downward_floor_recovery='floor recovery direction result=FAIL' not in text)
 if a.case=='body-ground':assertions.update(real_ground_wing_strike='ground wing strike result=PASS' in text)
 if a.case in ['body-curb','body-bump']:
  # The plastic push never moves material toward the body's underside.
  pushes=re.findall(r'\] yield frame=\d+ .*? local=\S+ \S+ (\S+)',text)
  assertions.update(plastic_push_never_down=all(float(z)>=-1e-4 for z in pushes))
 if a.case=='body-bump':
  probes=re.findall(r'\[bump-smoke\] scenario=(\S+) model=(\d+) variant=(\w+) repeat=(\d+) probe_speed=([-\d.]+)',text)
  # Speed kept 0.5 s after the approach ends: the same moment for both twins.
  strikes={(c,v,r):(float(k),float(d)) for c,v,r,k,d in re.findall(r'\[bump-smoke\] strike case=(\S+) variant=(\w+) repeat=(\d+) pre=\S+ keep10=\S+ keep_half=(\S+) max_drop=(\S+)',text)}
  inplace={(c,v,r):float(m) for c,v,r,m in re.findall(r'\[bump-smoke\] inplace case=(\S+) variant=(\w+) repeat=(\d+) moved=(\S+)',text)}
  ratios={(c,v):tuple(map(float,x.split(','))) for c,v,x in re.findall(r'\[bump-smoke\] strike case=(rest-\S+) variant=(\w+) repeat=0 .*? ratios=(\S+)',text)}
  speed={(c,v,r):float(x) for c,v,r,x in [(p[0],p[2],p[3],p[4]) for p in probes]}
  pairs=lambda d:[(k,d[k],d[(k[0],'stock')+k[2:]]) for k in d if k[1]=='material' and (k[0],'stock')+k[2:] in d]
  # Material and stock twins share the run, start and input: compare like with like.
  assertions.update(bump_sites_found='[bump-smoke] sites result=PASS' in text,bump_probes_complete=len(probes)>=60 and all((c,'stock',r) in speed for c,v,r in speed),bump_sequence_complete='[bump-smoke] complete result=PASS' in text,
   rest_suspension_matches_stock=bool(ratios) and all(max(abs(a-b) for a,b in zip(m,s))<.01 for k,m,s in pairs(ratios)),
   kerb_strike_keeps_momentum=all(m[0]>=.5*s[0] and m[1]<=max(.5,s[1]+.2) for k,m,s in pairs(strikes) if k[0].startswith('kerb')),
   kerb_drivable_in_place=all(m>=.5*s for k,m,s in pairs(inplace) if k[0].startswith('kerb') and s>4),
   drivable_after_bumps=bool(speed) and all(m>=.8*s for k,m,s in pairs(speed)))
  momentum={(c,v,r):tuple(map(float,x)) for c,v,r,*x in re.findall(r'\[bump-smoke\] momentum case=(\S+) variant=(\w+) repeat=(\d+) v0=(\S+) dv=\S+ response=\S+ shift=(\S+) forces=\S+ yields=\S+ yield_work=\S+ obstacle_side=(\S+)',text)}
  cooling={(c,v):float(x) for c,v,x in re.findall(r'\[bump-smoke\] strike case=(\S+) variant=(\w+) repeat=0 .*? cooling=(\S+)',text)}
  # No momentum leaves a kerb strike without an impulse or force (shift damping);
  # walls and poles still stop the car on its own side, with the radiator path kept.
  assertions.update(kerb_momentum_accounted=any(k[0].startswith('kerb') and k[1]=='material' for k in momentum) and all(abs(m[1])<=.05*max(m[0],1) for k,m in momentum.items() if k[0].startswith('kerb') and k[1]=='material'),
   walls_still_stop=all(m[2]<-1.0 for k,m in momentum.items() if k[0] in('wall8','pole8')) and any(k[0]=='wall8' for k in momentum),
   wall_radiator_damage_kept=cooling.get(('wall8','material'),0)>=.3)
 if a.case=='body-curb':
  # Wedged rest: still (< 0.2 m/s) for 30 frames while tilted 20-70 degrees, i.e.
  # held up against a wall or clipped into it rather than resting on wheels/side/roof.
  def wedged(text):
   up=1.0;still=0;hits=[];run=None
   for line in text.splitlines():
    m=re.search(r'\[curb-smoke\] run=(\d+) start',line)
    if m:run=m.group(1);still=0;continue
    u=re.search(r'\[curb-trace\] contacts .* up=(\S+) (\S+) (\S+)',line)
    if u:up=float(u.group(3));continue
    f=re.search(r'\[curb-smoke\] f=\d+ run=(\d+) .*vel=(\S+) (\S+) (\S+)',line)
    if f:
     speed=(float(f.group(2))**2+float(f.group(3))**2+float(f.group(4))**2)**.5
     still=still+1 if speed<.2 and .34<up<.94 else 0
     if still==30:hits.append((f.group(1),round(up,3)))
   return hits
  wedges=wedged(text)
  if wedges:print('wedged rests',wedges)
  runs=re.findall(r'\[curb-smoke\] run=(\d+) model=(\d+) speed=(\d+) angle=(\d+) kind=(\w+) repeat=(\d+) result=(PASS|FAIL) no_launch=(PASS|FAIL) no_fall_through=(PASS|FAIL) in_grid=(PASS|FAIL)',text)
  assertions.update(real_kerb_found='real kerb found result=PASS site=0' in text and 'real kerb found result=PASS site=1' in text,kerb_runs_complete=len(runs)>=40,no_launch=bool(runs) and all(r[7]=='PASS' for r in runs),no_fall_through=bool(runs) and all(r[8]=='PASS' for r in runs),stays_in_grid=bool(runs) and all(r[9]=='PASS' for r in runs),
   both_models=all(any(r[1]==m for r in runs) for m in ['211','130']),all_speeds=all(any(r[2]==s for r in runs) for s in ['10','20','30']),oblique_and_drop=any(r[3]!='0' for r in runs) and any(r[4]=='drop' for r in runs),repeated_bumps=any(r[5]=='3' for r in runs),
   material_depth_carried='stored depth mismatch' not in text,out_of_grid_safe='out-of-grid survived result=PASS' in text,no_wedged_rest=not wedges,inverted_rest_holds='[curb-smoke] inverted model=130 result=PASS' in text and '[curb-smoke] inverted model=211 result=PASS' in text,curb_sequence_complete='[curb-smoke] complete result=PASS' in text)
 if a.case=='body-bullets':
  for label in ['window preserves engine','partial wing remains connected','cut wing releases material','tail cut preserves wings wheels','damaged clump replacement preserves body','repair restores carrier mass','own weight flex']:assertions[label.replace(' ','_')]=label+' result=PASS' in text
  for stage in ['intact-tip','intact-root','partial-tip','loaded-tip','vacated-tip','removed-tip','retained-root','replacement-removed-tip','replacement-root','repaired-tip','repaired-root']:assertions['native_queries_'+stage]='native query '+stage+' result=PASS' in text
  assertions['no_body_failure']='[body-smoke]' in text and not re.search(r'\[body-smoke\][^\n]*result=FAIL',text)
 if a.case=='body-vehicles':
  receipts=re.findall(r'\[vehicle-body-smoke\] ([^\n]*?) result=(PASS|FAIL) class=([^ ]+) model=(\d+)',text)
  for label in ['automatic source admission','actual source sample','localized native bullet damage','native suspension preserved','damaged clump replacement','native repair']:
   for kind in ['car','bike','boat','helicopter','plane','dodo']:
    assertions[label.replace(' ','_')+'_'+kind]=any(name==label and result=='PASS' and vehicle_class==kind for name,result,vehicle_class,model in receipts)
  for label in ['native controller and rotating wheel queries','native bulletproof mission guard','native force remains sole motion owner','deletion invalidates source identity','native pool same address reused','reused pool has pristine source','many actors materialized','many actors remain finite']:
   assertions[label.replace(' ','_')]=any(name==label and result=='PASS' for name,result,kind,model in receipts)
  for stage in ['intact','intact-retained','damaged','damaged-retained','replacement','repaired']:
   for kind in ['car','bike','boat','helicopter','plane','dodo']:
    assertions['native_queries_'+kind+'_'+stage]=bool(re.search(r'\[vehicle-body-smoke\] native query result=PASS class='+kind+r' stage='+stage+r' ',text))
  assertions['no_vehicle_body_failure']='[vehicle-body-smoke] complete result=PASS' in text and not re.search(r'\[vehicle-body-smoke\][^\n]*result=FAIL',text)
  assertions['optional_extras_clump_replacement']='damaged clump replacement result=PASS class=extras model=170' in text
  assertions['concurrent_materialized_actors']='stress active=18 representatives=7' in text
 if a.case=='impact':assertions.update(visible_body_deformation='body impact deformation=PASS' in text,survivable_body_contact='survival=PASS' in text)
 if a.case in ['hit','external','graze']:assertions['physical_world_contact']=bool(re.search(r'(?:contacts_peak|contacts)=[1-9]',text))
 publication=[float(x)for x in re.findall(r'publication revision=\d+ ms=([\d.]+)',text)];steps=[float(x)for x in re.findall(r'step_max_ms=([\d.]+)',text)]
 body_times=[float(x)for x in re.findall(r'\[aircraft-body\] frame_ms=([\d.]+)',text)]
 vehicle_frames=[(int(n),float(ms))for n,ms in re.findall(r'\[vehicle-body\] actors=(\d+) frame_ms=([\d.]+)',text)]
 stress_frames=sorted(ms for n,ms in vehicle_frames if n>=24)
 body_render_times=[float(ms)for ms in re.findall(r'\[vehicle-body\] render_ms=([\d.]+)',text)]
 frames=[float(x)for x in re.findall(r'coordinator_frame_ms=([\d.]+)',text)]
 intervals=sorted(float(x)for x in re.findall(r'frame_interval_ms=([\d.]+)',text))
 metrics={'elapsed_s':time.monotonic()-start,'publication_samples':len(publication),'publication_mean_ms':statistics.mean(publication)if publication else None,'publication_max_ms':max(publication)if publication else None,'solver_step_max_ms':max(steps)if steps else None,'coordinator_frame_max_ms':max(frames)if frames else None,'body_frame_max_ms':max(body_times)if body_times else None,'body_frame_mean_ms':statistics.mean(body_times)if body_times else None,'vehicle_body_frame_max_ms':max(ms for n,ms in vehicle_frames)if vehicle_frames else None,'vehicle_body_actors_max':max(n for n,ms in vehicle_frames)if vehicle_frames else None,'vehicle_body_render_mean_ms':statistics.mean(body_render_times)if body_render_times else None,'vehicle_body_render_max_ms':max(body_render_times)if body_render_times else None,'many_actor_frame_mean_ms':statistics.mean(stress_frames)if stress_frames else None,'many_actor_frame_p95_ms':stress_frames[int(.95*(len(stress_frames)-1))]if stress_frames else None,'many_actor_frame_max_ms':max(stress_frames)if stress_frames else None,'frame_interval_median_ms':statistics.median(intervals)if intervals else None,'frame_interval_p95_ms':intervals[int(.95*(len(intervals)-1))]if intervals else None,'frame_interval_max_ms':max(intervals)if intervals else None}
 report={'case':a.case,'experimental_wing':a.experimental_wing,'application':str(app),'assertions':assertions,'metrics':metrics,'scope':'ordinary saved-game renderer and world; native default motion; committed vehicle material geometry' if a.case=='body-vehicles' else 'ordinary saved-game renderer and world; native default or opt-in bounded wing; static native COL'};(work/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
 if not all(assertions.values()):raise RuntimeError('Ordinary game assertions failed; see '+str(work/'report.json'))
if __name__=='__main__':main()
