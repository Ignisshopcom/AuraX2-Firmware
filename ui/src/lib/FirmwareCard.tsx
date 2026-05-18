import { useState } from 'preact/hooks'

export function FirmwareCard() {
  const [prog, setProg] = useState('')

  function updateFw(e: Event) {
    const file = (e.currentTarget as HTMLInputElement).files?.[0]
    if (!file) return
    setProg('Nahrávám firmware...')
    const fd = new FormData()
    fd.append('firmware', file, file.name)
    fetch('/update', { method: 'POST', body: fd })
      .then((r) => r.text())
      .then((t) => setProg(t))
      .catch(() => setProg('Chyba nahrávání'))
  }

  return (
    <div class="card fw-row">
      <span class="fw-label">Firmware (.bin)</span>
      <input type="file" accept=".bin" onChange={updateFw} class="fw-input" />
      {prog && <span class="prog" style="margin:0">{prog}</span>}
    </div>
  )
}
