# Ignis Studio 3.1 AuraX AXP Integration

This folder is a Git backup of the modified Ignis Studio files from:

`C:\Users\PC\Desktop\Ignis_Studio_3.1\resources\app`

The local app was updated directly. Ignis Studio itself is not a git repository
on this machine, so these copied files keep the AuraX `.axp` export changes
versioned together with the AuraX firmware.

Changed behavior:

- existing Photon / legacy `.pix` export remains unchanged
- export panel adds a `Technology` selector
- `Photon / legacy .pix` writes the original `.pix` format
- `AuraX compressed .axp` writes the new AuraX-only compressed format

Files mirrored here:

- `src/index.html`
- `src/js/processor.js`
- `src/js/project.js`
- `src/js/properties.js`
