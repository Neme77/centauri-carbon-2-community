# CC2 Control diagnostic export — privacy-safe v1

The opt-in **Settings → About → Download diagnostics** action downloads a local
JSON file, without sending it to GitHub or any external service. It uses only
three existing read-only endpoints: \`GET /api/health\`, \`GET /api/printer\`
and \`GET /api/canvas\`. No background polling is introduced by this feature.

## Data included

The JSON contains a schema version, creation timestamp and explicit per-source
availability (\`ok\`, \`invalid\` or \`unavailable\`). Selected health fields:
CC2 Control application version (strict numeric dotted version), uptime,
memory totals and MQTT/snapshot boolean flags. Selected printer fields:
connected flag, nozzle and bed temperatures, current Z offset/pending flag,
last numeric printer refusal method/code/age. Canvas fields: boolean auto
refill, connected module count and number of tray records (not their content).

Only known literal object keys and checked primitive values are emitted.
Responses, free-text messages, stack traces, filenames, G-code, live console
output, serial numbers/UUIDs, addresses, passwords, tokens and LAN codes
are NEVER copied verbatim. Unknown values become \`null\`. Failed endpoints
are marked unavailable without serializing their error body. Output size is
bounded; the browser initiates the download only on an explicit click.

**Review the file before sharing.** Even coarse timestamp, memory, errors
and temperature information can reveal device activity. This first version
is a point-in-time support snapshot, not a full system log or evidence of a
particular physical fault. A healthy API snapshot does not prove the printer
is safe to operate.

## Testing and release integration

The isolated browser regression injects obvious synthetic secret values into
all three backend replies and asserts that the downloaded JSON contains none
of them. It also simulates HTTP 503 for one endpoint and verifies a partial
report with no raw error response. No real printer or credentials are used.

When integrating into a release, rebuild the single-file UI, synchronize
deployed and firmware-overlay bundles/locales and refresh the exact builder
source snapshot and its pinned checksums. Require the standard eight CI gates
and real-printer acceptance before a signed application release.
