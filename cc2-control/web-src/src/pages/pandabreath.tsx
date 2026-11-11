import { ExternalLink } from 'lucide-preact'
import { Card, Page } from '@/components/ui/card'
import { t, tpl } from '@/lib/i18n'
import { pandabreath, pandabreathHost, pandabreathName } from '@/lib/pandabreath'
import { openSettings } from '@/lib/state'

const I = { size: 16, strokeWidth: 1 }

// The Panda Breath's own page, embedded as is. The frame is sandboxed to what that page needs: scripts and
// its own storage (control token), forms, prompts (token entry) and links that open a new tab.
export const PandaBreath = () => {
  const s = pandabreath.use()
  const name = pandabreathName(s)
  const host = pandabreathHost(s)
  if (!s.enabled)
    return (
      <Page title={name}>
        <Card>
          <p class="text-muted">{tpl('pandabreath.disabled', { name: t(name) })}</p>
          <button type="button" class="mt-2 text-cyan hover:underline" onClick={() => openSettings('integrations')}>
            {t('pandabreath.open_integrations')}
          </button>
        </Card>
      </Page>
    )
  const url = `http://${host}/`
  return (
    <Page
      title={name}
      sub="pandabreath.description"
      tags={
        <a
          class="inline-flex min-h-11 max-w-full items-center gap-2 rounded-md border border-edge bg-field px-3 py-2 text-sm text-cyan [overflow-wrap:anywhere] hover:border-cyan hover:bg-panel focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-cyan"
          href={url}
          target="_blank"
          rel="noopener noreferrer"
        >
          <ExternalLink {...I} />
          {t('pandabreath.open_in_new_tab')}
        </a>
      }
    >
      <iframe
        key={url}
        src={url}
        title={t(name)}
        sandbox="allow-scripts allow-same-origin allow-forms allow-modals allow-popups allow-popups-to-escape-sandbox"
        class="block h-[calc(100dvh-12rem)] min-h-[480px] w-full rounded-lg border border-edge bg-panel"
      />
      <p class="mt-2 text-xs text-muted">{tpl('pandabreath.frame_note', { host, name: t(name) })}</p>
    </Page>
  )
}
