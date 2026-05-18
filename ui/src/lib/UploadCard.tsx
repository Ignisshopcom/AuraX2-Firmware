import { useState } from 'preact/hooks'

export function UploadCard({ onUploaded }: { onUploaded?: () => void }) {
  const [prog, setProg] = useState('')

  function upload(e: Event) {
    const file = (e.currentTarget as HTMLInputElement).files?.[0]
    if (!file) return
    setProg('Nahrávám...')
    const fd = new FormData()
    fd.append('file', file, file.name)
    fetch('/upload', { method: 'POST', body: fd })
      .then((r) => r.text())
      .then((t) => { setProg(t); onUploaded?.() })
      .catch(() => setProg('Chyba nahrávání'))
  }

  return (
    <div class="card">
      <div class="dz">
        <p>Nahrát .pix soubor</p>
        <input type="file" accept=".pix" onChange={upload} />
        {prog && <p class="prog">{prog}</p>}
      </div>
    </div>
  )
}
