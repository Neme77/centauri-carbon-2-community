import { FileText } from 'lucide-preact'
import { t } from '@/lib/i18n'
import { useFilePicture } from '@/lib/pictures'

// The picture the slicer put in a G-code file, or a file icon when it has none (or when `path` is empty: the
// file is gone). `row`: the small one of a list row, where the name next to it says what it shows; otherwise
// the large one of a card. The Queue, Files and History lists share it, so a file looks the same everywhere.
export const FilePicture = ({
  storage,
  path,
  tag,
  row,
}: {
  storage: string
  path: string
  tag?: string
  row?: boolean
}) => {
  const url = useFilePicture(storage, path, tag)
  return (
    <div
      data-file-picture={url ? 'shown' : 'none'}
      class={`grid shrink-0 place-items-center overflow-hidden border border-edge bg-well ${
        row ? 'size-12 rounded-md' : 'size-28 rounded-lg'
      }`}
    >
      {url ? (
        <img
          src={url}
          alt={row ? '' : t('files.g_code_model_preview')}
          draggable={false}
          class={`size-full object-contain ${row ? 'p-0.5' : 'p-1.5'}`}
        />
      ) : (
        <FileText size={row ? 22 : 40} strokeWidth={1} class="text-edge" />
      )}
    </div>
  )
}
