// The turning instruction of the screw page: the rule (stages 0-3), the units, and the number formats. No browser.
// The tables are the ones of the accepted draft (ENTWURF-DREHANGABE.md 2.3, run with rechne-drehangabe.mjs).
import assert from 'node:assert/strict'
import {
  CLICK_DEG,
  clockHours,
  COARSE_SAFETY,
  ONE_GO_DEG,
  TURN_TOLERANCE_UM,
  turnExact,
  turnFraction,
  turnFractionAid,
  turnsNumber,
  turnPlan,
  heightLabel,
} from '../src/lib/screws.ts'

assert.deepEqual([CLICK_DEG, TURN_TOLERANCE_UM, ONE_GO_DEG, COARSE_SAFETY], [5, 20, 180, 0.97])
const near = (a, b, d = 0.05) => assert.ok(Math.abs(a - b) <= d, `${a} is not ${b}`)

// P = 744 µm per turn: 1 degree = 2.067 µm, 1 click = 10.33 µm.
const small = turnPlan(35, 744)
assert.equal(small.stage, 1)
assert.deepEqual([small.fine.n, small.fine.q, small.fine.r, small.fine.deg], [3, 0, 3, 15])
near(small.rest, 4.0)
near(turnPlan(95, 744).rest, 2.0)
assert.equal(turnPlan(95, 744).fine.n, 9)
const quarter = turnPlan(186, 744)
assert.equal(quarter.stage, 2)
assert.deepEqual([quarter.fine.q, quarter.fine.r, quarter.fine.deg], [1, 0, 90])
near(quarter.rest, 0)
const normal = turnPlan(213, 744)
assert.equal(normal.stage, 2)
near(normal.theta, 103.1, 0.05)
assert.deepEqual([normal.fine.q, normal.fine.r, normal.fine.deg], [1, 3, 105]) // 90° + 3 clicks
near(normal.rest, -4.0)
const half = turnPlan(372, 744)
assert.equal(half.stage, 2, 'half a turn is still one instruction')
assert.equal(half.fine.deg, 180)
// Stage 3: coarse first, then measure all four, then fine.
for (const [m, coarse, after, fine, rest] of [
  [560, 180, 188, 90, 2.0],
  [800, 360, 56, 25, 4.3],
  [1500, 630, 198, 95, 1.7],
]) {
  const x = turnPlan(m, 744)
  assert.equal(x.stage, 3, `${m} µm`)
  assert.equal(x.coarse, coarse, `${m} µm coarse`)
  near(x.afterCoarse, after, 1)
  assert.equal(x.fine.deg, fine, `${m} µm fine`)
  near(x.rest, rest, 0.1)
}
// P = 700 µm per turn (the CC2's M4, the value the page uses): half a turn is already stage 3 at 372 µm.
const p700 = turnPlan(372, 700)
assert.equal(p700.stage, 3)
assert.deepEqual([p700.coarse, p700.fine.deg], [180, 10])
// Direction: the sign of the movement.
assert.equal(turnPlan(213, 744).dir, 'raise')
assert.equal(turnPlan(-213, 744).dir, 'lower')
near(turnPlan(-213, 744).rest, 4.0)

// The limits: tolerance, 90°, one go, coarse.
assert.equal(turnPlan(20, 744).stage, 0, '20 µm: do nothing')
assert.equal(turnPlan(-20, 744).stage, 0)
assert.equal(turnPlan(21, 744).stage, 1, '21 µm: turn')
assert.equal(turnPlan(0, 744).stage, 0)
assert.equal(turnPlan(Number.NaN, 744).stage, 0, 'no number: nothing to turn')
assert.equal(turnPlan(174.9, 720).stage, 1, 'just under 90° after rounding to clicks')
assert.equal(turnPlan(175, 720).stage, 2, '87.5° rounds to 18 clicks = 90°')
assert.equal(turnPlan(360, 720).stage, 2, 'exactly 180°')
assert.equal(turnPlan(360.2, 720).stage, 3, 'just over 180°: two steps')
assert.equal(turnPlan(360.2, 720).coarse, 90)
// Properties over many movements: the coarse step never passes the goal, the reading aid adds up, clicks are whole.
for (let m = 21; m <= 3000; m += 7) {
  const x = turnPlan(m, 700)
  const upd = 700 / 360
  assert.ok(x.theta > 0)
  assert.ok(Number.isInteger(x.fine.n) && x.fine.n >= 0 && x.fine.r >= 0 && x.fine.r < 18)
  assert.equal(x.fine.q * 18 + x.fine.r, x.fine.n)
  if (x.stage === 3) {
    assert.ok(x.coarse % 90 === 0 && x.coarse <= 0.97 * x.theta + 1e-9, `coarse never passes the goal (${m})`)
    near(x.afterCoarse, m - x.coarse * upd, 1e-9)
    near(x.rest, m - x.coarse * upd - x.fine.deg * upd, 1e-9)
  } else {
    near(x.rest, m - x.fine.deg * upd, 1e-9)
    assert.ok(x.theta <= 180 + 1e-9)
  }
  const mirrored = turnPlan(-m, 700)
  assert.equal(mirrored.stage, x.stage)
  near(mirrored.rest, -x.rest, 1e-9)
}

// The units come from the same exact angle; nothing is rounded to clicks. 213 µm at 744: 103.1°, 20.6 clicks, 17.2 minutes; as a fraction on the clock grid (twelfths) it is 1/4.
const th = normal.theta
assert.equal(turnExact(th, 'degrees'), '103.1°')
assert.equal(turnExact(th, 'clicks'), '20.6')
assert.equal(turnExact(th, 'fractions'), '0.286')
assert.equal(turnFraction(th), '1/4')
assert.equal(turnExact(th, 'clock'), '0:17')
assert.equal(turnExact(th, 'degrees', 'de'), '103,1°')
assert.equal(turnExact(th, 'clock', 'de'), '0:17', 'whole minutes: no decimal sign')
const big = turnPlan(1500, 744).theta
assert.equal(turnExact(big, 'degrees'), '725.8°')
assert.equal(turnFraction(big), '2')
assert.equal(turnFraction(turnPlan(800, 744).theta), '1 + 1/12')
assert.equal(turnFraction(turnPlan(372, 744).theta), '1/2')
assert.equal(turnFraction(0), '0')
assert.equal(turnExact(359.96, 'clock'), '1:00', 'the clock carries over to the next hour')
assert.equal(turnExact(53.99, 'clock'), '0:09')
assert.equal(turnExact(60, 'clock'), '0:10')
assert.equal(turnExact(480, 'clock'), '1:20', 'Klipper style: one hour twenty')
// The clock in hours: the handle starts at 12, one turn is twelve hours, rounded to quarter hours.
assert.deepEqual(clockHours(0), { turns: 0, onTwelve: true, time: '12:00' })
assert.deepEqual(clockHours(7.5), { turns: 0, onTwelve: false, time: '12:15' })
assert.deepEqual(clockHours(84), { turns: 0, onTwelve: false, time: '2:45' }, '14 minutes on the minute scale')
assert.deepEqual(clockHours(90), { turns: 0, onTwelve: false, time: '3:00' })
assert.deepEqual(clockHours(359.96), { turns: 1, onTwelve: true, time: '12:00' }, 'carries over to a whole turn')
assert.deepEqual(clockHours(480), { turns: 1, onTwelve: false, time: '4:00' })
assert.equal(turnExact(84, 'clockhours'), '2:45')
assert.equal(turnExact(480, 'clockhours'), '1 + 4:00')
// Against the clock the hand runs backwards from 12 (the direction itself is decided elsewhere and not changed here):
// 1.5 hours = 45°, 1.75 hours = 52.5°, 0.5 hours = 15°; whole turns in front as before.
assert.equal(clockHours(45).time, '1:30', 'clockwise: forwards')
assert.equal(clockHours(45, true).time, '10:30', '1.5 hours counter-clockwise')
assert.equal(clockHours(52.5, true).time, '10:15', '1.75 hours counter-clockwise')
assert.equal(clockHours(15, true).time, '11:30', '0.5 hours counter-clockwise')
assert.deepEqual(clockHours(405, true), { turns: 1, onTwelve: false, time: '10:30' }, 'a whole turn, then backwards')
assert.deepEqual(clockHours(360, true), { turns: 1, onTwelve: true, time: '12:00' }, 'whole turns alone end on 12')
assert.equal(turnExact(45, 'clockhours', 'en', true), '10:30')
assert.equal(turnExact(405, 'clockhours', 'en', true), '1 + 10:30')
// Back from every unit to the angle: one decimal is far below the measuring scatter (0.1° ≈ 0.2 µm).
for (const t of [7.3, 16.9, 46, 103.1, 387.1, 725.8]) {
  near(Number.parseFloat(turnExact(t, 'degrees')), t, 0.05)
  near(Number.parseFloat(turnExact(t, 'clicks')) * 5, t, 0.25)
  near(Number.parseFloat(turnExact(t, 'fractions')) * 360, t, 0.18)
}
// Heights: whole micrometres or millimetres with three decimals, the unit behind the number, the decimal sign by language.
assert.equal(heightLabel(-0.004, 'um', true), '−4 µm')
assert.equal(heightLabel(-0.004, 'mm', true, 'de'), '−0,004 mm')
assert.equal(heightLabel(0.7, 'mm', false, 'en'), '0.700 mm')
assert.equal(heightLabel(-2.155, 'um'), '−2155 µm', 'no thousands separator')
// Auto in fractions: only fractions of a clock face (multiples of 30 degrees): whole turns, quarters, at most 1/12 or 1/6.
const frac = um => turnFractionAid((um * 360) / 700)
assert.deepEqual([frac(78).text, frac(78).deg], ['1/12', 30], '78 µm = 40.1° is a twelfth')
assert.deepEqual([frac(114).text, frac(114).deg], ['1/6', 60], '114 µm = 58.6° is a sixth')
assert.deepEqual([frac(200).text, frac(200).deg], ['1/4', 90], '200 µm = 102.9° is a quarter')
const rest = um => heightLabel((um - (frac(um).deg * 700) / 360) / 1000, 'um', true).replace(/\u00a0/g, ' ')
assert.deepEqual([rest(78), rest(114), rest(200)], ['+20 µm', '−3 µm', '+25 µm'], 'the rests of the three examples')
for (const [deg, text] of [[0, '0'], [15, '0'], [30, '1/12'], [60, '1/6'], [90, '1/4'], [120, '1/4 + 1/12'], [150, '1/4 + 1/6'], [180, '1/2'], [270, '3/4'], [360, '1'], [390, '1 + 1/12'], [450, '1 + 1/4']])
  assert.equal(turnFractionAid(deg).text, text, `${deg}°`)
for (const bad of ['1/8', '1/24', '5/16']) for (let deg = 0; deg <= 720; deg += 0.5) assert.notEqual(turnFractionAid(deg).text.includes(bad), true, `${deg}°: no ${bad}`)
for (let deg = 0; deg <= 540; deg += 0.5) {
  const a = turnFractionAid(deg)
  assert.equal(a.deg % 30, 0, `${deg}°: only multiples of 30 degrees`)
  assert.ok(Math.abs(deg - a.deg) <= 15, `${deg}°: the rest is at most half a twelfth`)
}
assert.equal(turnsNumber(90, 'en'), '0.25')
assert.equal(turnsNumber(390, 'de'), '1,08', 'two decimals, comma by language')
assert.equal(turnsNumber(360, 'en'), '1')
console.log('PASS screw turning instruction: stages, limits, units, formats')
