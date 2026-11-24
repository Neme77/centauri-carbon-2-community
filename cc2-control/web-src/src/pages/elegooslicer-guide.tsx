import { useState } from 'preact/hooks'
import { Copy } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { copyText } from '@/lib/clipboard'
import { errText, notify } from '@/lib/api'
import { t } from '@/lib/i18n'

const G = { size: 16, strokeWidth: 1 }

// The printer address as the person reached this page; CC2 Control's own port stays 8081.
const lines = (host: string) => [
  '"host_type": "octoprint",',
  `"print_host": "${host}:8081",`,
  `"print_host_webui": "http://${host}:8081",`,
  '"bbl_use_print_host_webui": "1",',
  '"printhost_authorization_type": "key",',
]

// Experimental: ElegooSlicer keeps only its own printer connections, but a printer profile with these
// OctoPrint-style entries makes its Device tab show CC2 Control and sends prints to CC2 Control.
export const ElegooSlicerGuide = ({ row }: { row: string }) => {
  const [open, setOpen] = useState(false)
  // A local address cannot be reached from the slicer's computer: show a typical printer address instead and say so.
  const local =
    !location.hostname || ['localhost', '::1', '[::1]'].includes(location.hostname) || /^127\./.test(location.hostname)
  const host = local ? '192.168.1.50' : location.hostname
  const added = lines(host)
  const copy = async () => {
    try {
      await copyText(added.join('\n'))
      notify(t('settings.elegooslicer_copied'))
    } catch (e) {
      notify(errText(e), 'error')
    }
  }
  return (
    <>
      <div class={row}>
        <b>{t('settings.elegooslicer_title')}</b>
        <span class="text-muted">{t('settings.elegooslicer_summary')}</span>
        <Button data-elegooslicer-toggle aria-expanded={open} onClick={() => setOpen(!open)}>
          {t(open ? 'settings.elegooslicer_hide_guide' : 'settings.elegooslicer_show_guide')}
        </Button>
      </div>
      {open && (
        <div data-elegooslicer-guide class="my-2 rounded-md border border-edge px-4 py-3">
          <p class="mb-3">{t('settings.elegooslicer_intro')}</p>
          <ol class="list-decimal space-y-3 pl-5">
            <li>{t('settings.elegooslicer_step_copy')}</li>
            <li>
              {t('settings.elegooslicer_step_open')}
              <code class="mt-1 block break-all rounded bg-field px-2 py-1">
                ~/Library/Application Support/ElegooSlicer/user/default/machine/
              </code>
              <span class="text-muted">{t('settings.elegooslicer_path_other')}</span>
            </li>
            <li>
              {t('settings.elegooslicer_step_edit')}
              <span class="mt-2 block text-muted">{t('settings.elegooslicer_example_caption')}</span>
              <section
                aria-label={t('settings.elegooslicer_example_caption')}
                // biome-ignore lint/a11y/noNoninteractiveTabindex: The bounded scroll region must be reachable with the keyboard.
                tabIndex={0}
                class="mt-1 overflow-x-auto rounded bg-field"
              >
                <pre data-elegooslicer-example class="px-3 py-2 text-xs">
                  <span>{'{'}</span>
                  {'\n'}
                  {added.map(line => (
                    <span class="text-green">
                      {'    '}
                      {line}
                      {'\n'}
                    </span>
                  ))}
                  <span>
                    {
                      '    "from": "User",\n    "inherits": "Elegoo Centauri Carbon 2 0.6 nozzle",\n    "name": "CC2 Control",\n    …\n}'
                    }
                  </span>
                </pre>
              </section>
              <span class="mt-1 block text-muted">{t('settings.elegooslicer_edit_note')}</span>
              {local && (
                <span data-elegooslicer-localhost role="note" class="mt-1 block text-amber">
                  {t('settings.elegooslicer_localhost_hint')}
                </span>
              )}
              <Button class="mt-2" data-elegooslicer-copy onClick={copy}>
                <Copy {...G} />
                {t('settings.elegooslicer_copy_lines')}
              </Button>
            </li>
            <li>{t('settings.elegooslicer_step_choose')}</li>
            <li>{t('settings.elegooslicer_step_print')}</li>
          </ol>
          <p data-elegooslicer-warning role="note" class="mt-3 rounded-md border border-amber px-3 py-2 text-amber">
            {t('settings.elegooslicer_warning')}
          </p>
        </div>
      )}
    </>
  )
}
