import { Check } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { t } from '@/lib/i18n'
import { SCREW_VERDICT_TONE, type ScrewVerdict } from '@/lib/screws'

// The status of one screw corner in the "Measured" table: a word in the colour of the plan's verdict for that
// corner (SCREW_VERDICT_TONE, the table the suggestion uses too). No badge, no frame, no fill. "Within tolerance" carries a
// tick; "Too low" and "Too high" are coloured text only, without a symbol (decided 20 Nov). Too low = the plan says raise, too
// high = the plan says lower.
export const StatusMark = ({ verdict, corner }: { verdict: ScrewVerdict; corner: string }) => (
  <span
    class={cn('inline-flex items-center gap-1 font-semibold sm:whitespace-nowrap', SCREW_VERDICT_TONE[verdict])}
    data-screw-verdict={corner}
    data-screw-verdict-kind={verdict}
  >
    {verdict === 'ok' && <Check size={14} strokeWidth={2} aria-hidden="true" class="shrink-0" />}
    {t(verdict === 'ok' ? 'bed.lvl_in_order' : verdict === 'raise' ? 'bed.screw_status_low' : 'bed.screw_status_high')}
  </span>
)
