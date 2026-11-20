import { parseScrewStrategy, type ScrewStrategy } from './screws'
import { ss, store } from './store'

// The strategy of the screw adjustment plan the operator chose. Standard (front left is the fixed reference) is the
// default; the optimized plan, which turns the front-left screw as well, is only ever used after an explicit choice. The
// choice is kept in this browser tab (sessionStorage), so it holds over every further measurement and a reload, and
// comes back to standard only by the explicit "Back to standard". `kept`: the offer of the optimized plan was declined
// (or the operator came back to standard) and is not shown again in this tab. The page works without the storage too.
const KEY = 'cc2-screw-strategy'
const KEPT_KEY = 'cc2-screw-strategy-kept'
export const screwStrategy = store<{ strategy: ScrewStrategy; kept: boolean }>({
  strategy: parseScrewStrategy(ss.get(KEY)),
  kept: ss.get(KEPT_KEY) === '1',
})
export const chooseOptimized = () => {
  screwStrategy.set({ strategy: 'optimized', kept: false })
  ss.set(KEY, 'optimized')
  ss.del(KEPT_KEY)
}
export const returnToStandard = () => {
  screwStrategy.set({ strategy: 'standard', kept: true })
  ss.set(KEY, 'standard')
  ss.set(KEPT_KEY, '1')
}
export const keepStandard = () => {
  screwStrategy.set({ kept: true })
  ss.set(KEPT_KEY, '1')
}
