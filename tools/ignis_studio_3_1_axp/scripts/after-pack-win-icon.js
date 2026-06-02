const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

module.exports = async function afterPackWinIcon(context) {
  if (context.electronPlatformName !== 'win32') return;

  const projectDir = context.packager.info.projectDir;
  const exeName = `${context.packager.appInfo.productFilename}.exe`;
  const exePath = path.join(context.appOutDir, exeName);
  const iconPath = path.join(projectDir, 'src', 'img', 'icon.ico');
  const rceditPath = path.join(projectDir, 'node_modules', 'electron-winstaller', 'vendor', 'rcedit.exe');

  if (!fs.existsSync(exePath)) {
    throw new Error(`Cannot set Ignis icon, EXE was not found: ${exePath}`);
  }
  if (!fs.existsSync(iconPath)) {
    throw new Error(`Cannot set Ignis icon, icon was not found: ${iconPath}`);
  }
  if (!fs.existsSync(rceditPath)) {
    throw new Error(`Cannot set Ignis icon, rcedit was not found: ${rceditPath}`);
  }

  execFileSync(rceditPath, [
    exePath,
    '--set-icon', iconPath,
    '--set-version-string', 'CompanyName', 'IgnisShop',
    '--set-version-string', 'FileDescription', 'Ignis Studio',
    '--set-version-string', 'ProductName', 'Ignis Studio',
    '--set-version-string', 'InternalName', 'Ignis Studio',
    '--set-version-string', 'OriginalFilename', exeName
  ], { stdio: 'inherit' });
};
