// Right after a print is stopped the printer still reports the job for a moment (measured on the printer: at most
// about 2.7 s, 5 of 5), and CC2 Control refuses to delete with file_delete_active_print_unknown until the state is
// clear. The page tries again once a second for at most DELETE_RETRY_MS; every other refusal, and the active print
// itself (file_delete_active_print), is shown at once. The guard on the printer side is unchanged.
export const DELETE_RETRY_MS = 5000
const UNCLEAR = 'file_delete_active_print_unknown'
type Reply = { ok: boolean; clone: () => { json: () => Promise<any> } }
export async function deleteRetrying<R extends Reply>(
  fetchFn: (url: string, init: any) => Promise<R>,
  url: string,
  init: any,
  sleep: (ms: number) => Promise<void> = ms => new Promise(done => setTimeout(done, ms))
): Promise<R> {
  let waited = 0
  for (;;) {
    const reply = await fetchFn(url, init)
    if (reply.ok || waited + 1000 > DELETE_RETRY_MS) return reply
    const data = await reply
      .clone()
      .json()
      .catch(() => null)
    if (data?.code !== UNCLEAR) return reply
    await sleep(1000)
    waited += 1000
  }
}
