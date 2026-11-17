import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'

// A USB read that stops answering (PR #390 review): while it hangs the dialog says so and asks to remove the stick,
// and "start" stays grey; once the read is free again it says so, with the reason it ended (an error: check the file
// or try again; no error: only a timeout); if it still hangs a while after the stick was removed (power_cycle, 60 s
// in the backend), it advises to switch the printer off and on. The state comes from the status usb_stall.
const read = file => readFileSync(new URL(`../src/${file}`, import.meta.url), 'utf8')
const dialog = read('pages/print-dialog.tsx')
assert.match(dialog, /data\?\.usb_stall/, 'the dialog follows the status usb_stall')
assert.match(dialog, /data\?\.stall/, 'it takes the stall it ran into from the refusal')
assert.match(dialog, /s\.power_cycle \? 'print\.usb_stall_power_cycle' : 'print\.usb_stall_hanging'/, 'hanging: remove it, or power cycle')
assert.match(dialog, /print\.usb_stall_ready/, 'free again')
assert.match(dialog, /s\.outcome === 'read_error' \? 'print\.usb_stall_read_error' : 'print\.usb_stall_slow'/, 'with the reason')
assert.match(dialog, /disabled=\{busy \|\| meshChecking \|\| stallHangs/, 'start stays grey while the read hangs')
assert.match(dialog, /if \(stallHangs\) return/, 'and a start is not sent meanwhile')
const de = JSON.parse(readFileSync(new URL('../public/locales/de.json', import.meta.url), 'utf8'))
assert.equal(de['print.usb_stall_hanging'], 'USB-Stick antwortet nicht – der Lesevorgang hängt. Bitte den Stick abziehen.')
assert.equal(de['print.usb_stall_ready'], 'Wieder bereit – Stick einstecken und erneut versuchen.')
assert.equal(de['print.usb_stall_power_cycle'], 'Der Lesevorgang hängt weiterhin. Drucker aus- und wieder einschalten.')
assert.equal(de['print.usb_stall_read_error'], 'Ladevorgang fehlerhaft – Datei prüfen oder erneut versuchen.')
assert.equal(de['print.usb_stall_slow'], 'Zeitüberschreitung')
for (const lang of ['en', 'de', 'fr', 'es', 'it', 'pl', 'cs', 'hu', 'tr', 'ru', 'zh']) {
  for (const dir of ['../public/locales', '../../web/locales']) {
    const s = JSON.parse(readFileSync(new URL(`${dir}/${lang}.json`, import.meta.url), 'utf8'))
    for (const key of ['print.usb_stall_hanging', 'print.usb_stall_power_cycle', 'print.usb_stall_ready', 'print.usb_stall_read_error'])
      assert.ok((s[key] ?? '').length > 5, `${lang}: ${key}`)
    assert.ok((s['print.usb_stall_slow'] ?? '').length >= 2, `${lang}: print.usb_stall_slow`)
  }
}
console.log('PASS hanging USB read: hanging, free again with its reason, power cycle advice, every language')
