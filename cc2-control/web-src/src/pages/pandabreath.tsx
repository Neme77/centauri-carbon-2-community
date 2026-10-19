import { ExternalLink } from 'lucide-preact'
import { Card, Page } from '@/components/ui/card'
import { t, tpl } from '@/lib/i18n'
import { pandabreath } from '@/lib/pandabreath'
import { openSettings } from '@/lib/state'

const I = { size: 16, strokeWidth: 1 }

// The Panda Breath's own page, embedded as is. The frame is sandboxed to what that page needs: scripts and
// its own storage (control token), forms, prompts (token entry) and links that open a new tab.
export const PandaBreath = () => {
  const { enabled, host } = pandabreath.use()
  if (!enabled)
    return (
      <Page title="common.pandabreath">
        <Card>
          <p class="text-muted">{t('pandabreath.disabled')}</p>
          <button type="button" class="mt-2 text-cyan hover:underline" onClick={() => openSettings('integrations')}>
            {t('pandabreath.open_integrations')}
          </button>
        </Card>
      </Page>
    )
  const url = `http://${host}/`
  return (
    <Page
      title="common.pandabreath"
      sub="pandabreath.description"
      tags={
        <a
          class="flex items-center gap-1.5 text-sm text-cyan hover:underline"
          href={url}
          target="_blank"
          rel="noreferrer"
        >
          <ExternalLink {...I} />
          {t('pandabreath.open_in_new_tab')}
        </a>
      }
    >
      <iframe
        key={url}
        src={url}
        title={t('common.pandabreath')}
        sandbox="allow-scripts allow-same-origin allow-forms allow-modals allow-popups allow-popups-to-escape-sandbox"
        class="block h-[calc(100dvh-12rem)] min-h-[480px] w-full rounded-lg border border-edge bg-panel"
      />
      <p class="mt-2 text-xs text-muted">{tpl('pandabreath.frame_note', { host })}</p>
    </Page>
  )
}
