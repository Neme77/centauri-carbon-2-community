import { Tag } from '@/components/ui/badge'
import { spoolmanLabel, type Spool } from '@/lib/spools'

export const SpoolmanId = ({ spool }: { spool: Spool }) =>
  spool.spoolman_id ? (
    <Tag class="max-w-full justify-self-start break-all font-mono tabular-nums" data-spoolman-id={spool.spoolman_id}>
      {spoolmanLabel(spool)}
    </Tag>
  ) : null
