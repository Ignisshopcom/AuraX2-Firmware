module.exports = {
  packagerConfig: {
    name: 'Ignis Studio',
    executableName: 'Ignis Studio',
    icon: 'src/img/icon',
    asar: false,
  },
  rebuildConfig: {},
  makers: [
    {
      name: '@electron-forge/maker-squirrel',
      platforms: ['win32'],
      config: {
        name: 'ignis_studio',
        setupExe: 'Ignis Studio Setup.exe',
        setupIcon: 'src/img/icon.ico',
      },
    },
    {
      name: '@electron-forge/maker-zip',
      platforms: ['win32', 'darwin'],
    },
    {
      name: '@electron-forge/maker-dmg',
      platforms: ['darwin'],
      config: {
        name: 'Ignis Studio',
        icon: 'src/img/icon.icns',
      },
    },
  ],
};
