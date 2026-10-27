import assert from 'node:assert/strict'
import { CompletionTracker, replayable } from '../src/lib/print-completion.ts'
const sample = (state, status, path='part.gcode', uuid='job-1') => ({connected:true,last_message_age:0,machine:{status},print:{uuid,filename:path,state,native:{state,filename:path,duration:123.5}}})
let seen=new Set(), tracker=new CompletionTracker(seen)
assert.equal(tracker.observe(sample('standby',1),true),null)
tracker.observe(sample('printing',2),true)
assert.equal(tracker.observe(sample('complete',1),false),null)
assert.equal(tracker.observe({...sample('complete',1),connected:false},true),null)
assert.equal(tracker.observe({...sample('complete',1),last_message_age:60},true),null)
let event=tracker.observe(sample('complete',1),true);assert(event?.live);assert.equal(event.path,'part.gcode')
assert.equal(tracker.observe(sample('complete',1),true),null)
assert.equal(tracker.observe(sample('complete',1,'part.gcode',''),true),null)
seen.add(event.key);seen.add(event.alias);tracker=new CompletionTracker(seen)
assert.equal(tracker.observe(sample('complete',1,'part.gcode',''),true),null)
tracker.observe(sample('printing',2,'part.gcode','job-2'),true)
event=tracker.observe(sample('complete',1,'part.gcode','job-2'),true);assert(event?.live,'New UUID must allow a second same-file print')
tracker=new CompletionTracker(new Set());event=tracker.observe(sample('complete',1),true);assert(event&&!event.live,'Opening after completion is a quiet last-job notice')
for(const state of ['cancelled','error','standby']){tracker=new CompletionTracker(new Set());tracker.observe(sample('printing',2),true);assert.equal(tracker.observe(sample(state,1),true),null)}
tracker=new CompletionTracker(new Set());tracker.observe(sample('printing',2),true);assert.equal(tracker.observe(sample('complete',1,'other.gcode'),true),null)
for(const path of ['/root/file.gcode','../file.gcode','a/../file.gcode','a\\b.gcode',''])assert.equal(replayable(path),false)
assert(replayable('parts/A B.gcode'))
console.log('PASS explicit completion, cancellation/stale/mismatched refusal, deduplication, repeat prints, late opening and replay paths')

tracker=new CompletionTracker(new Set());for(let i=0;i<2;i++){tracker.observe(sample('printing',2,'part.gcode',''),true);assert(tracker.observe(sample('complete',1,'part.gcode',''),true)?.live,'Same file without UUID must still report each observed print')}

tracker=new CompletionTracker(new Set());tracker.observe(sample('printing',2),true)
const cleared=sample('complete',1,'');cleared.print.native.duration=null;cleared.print.duration=0
assert.equal(tracker.observe(cleared,true)?.path,'part.gcode','Use the observed file when firmware clears end metadata')

// Native UDS names internal storage as local/, MQTT names the same file without it.
// Completion must be delivered before any home, including with no axes homed.
for (const axes of ['', 'xy', 'xyz']) {
  tracker = new CompletionTracker(new Set())
  const running = sample('printing', 2, 'CC2_USB_test.gcode', '')
  running.print.native = null
  running.motion = { homed_axes: axes, x: 52.5, y: 264, z: 80 }
  tracker.observe(running, true)
  const done = sample('complete', 1, '', '')
  done.print.native = { state: 'complete', filename: 'local/CC2_USB_test.gcode', duration: 2151.2 }
  done.motion = running.motion
  const result = tracker.observe(done, true)
  assert(result?.live, `Completion must not wait for home (${axes}) or a path-source change`)
  assert.equal(result.path, 'CC2_USB_test.gcode', 'Replay uses an internal relative path')
  assert.equal(tracker.observe(done, true), null, 'Same native completion is emitted once')
}
tracker = new CompletionTracker(new Set())
tracker.observe(sample('printing', 2, 'a/part.gcode'), true)
assert.equal(tracker.observe(sample('complete', 1, 'local/b/part.gcode'), true), null,
  'Normalizing the storage prefix must still reject a genuinely different file')
console.log('PASS local/ UDS vs MQTT paths, no/XY/XYZ homing independence and different-file rejection')
