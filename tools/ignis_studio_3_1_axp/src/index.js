const path = require('path');
const { app, BrowserWindow, dialog, ipcMain, nativeImage, shell } = require('electron');

const HELP_URL = 'https://ignisshop.com/soubory/info.html';
const APP_ICON = path.join(__dirname, 'img', 'icon.ico');
const APP_ICON_IMAGE = nativeImage.createFromPath(APP_ICON);

if (require('electron-squirrel-startup')) {
  app.quit();
}

if (typeof app.disableDomainBlockingFor3DAPIs === 'function') {
  app.disableDomainBlockingFor3DAPIs();
}
app.commandLine.appendSwitch('ignore-gpu-blocklist');
app.setAppUserModelId('com.darkyork.ignisstudio');
app.setName('Ignis Studio');

let mainWindow = null;
let pendingProjectFile = getProjectPathFromArgs(process.argv);

function getProjectPathFromArgs(args) {
  for (const arg of args || []) {
    if (typeof arg === 'string' && path.extname(arg).toLowerCase() === '.ipr') {
      return arg;
    }
  }

  return null;
}

function openProjectFile(filePath) {
  if (!filePath) return;

  if (mainWindow && mainWindow.webContents && !mainWindow.webContents.isLoading()) {
    mainWindow.webContents.send('open-project-file', filePath);
    return;
  }

  pendingProjectFile = filePath;
}

const gotSingleInstanceLock = app.requestSingleInstanceLock();

if (!gotSingleInstanceLock) {
  app.quit();
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1280,
    height: 900,
    minWidth: 1024,
    minHeight: 768,
    icon: APP_ICON_IMAGE.isEmpty() ? APP_ICON : APP_ICON_IMAGE,
    frame: false,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      nodeIntegration: true,
      contextIsolation: false,
      sandbox: false,
      webSecurity: true,
      allowRunningInsecureContent: false,
      devTools: true,
    },
  });

  if (!APP_ICON_IMAGE.isEmpty()) {
    mainWindow.setIcon(APP_ICON_IMAGE);
  }

  mainWindow.maximize();
  mainWindow.loadFile(path.join(__dirname, 'index.html'));
  mainWindow.webContents.once('did-finish-load', () => {
    if (pendingProjectFile) {
      openProjectFile(pendingProjectFile);
      pendingProjectFile = null;
    }
  });
  mainWindow.once('ready-to-show', () => {
    if (mainWindow) mainWindow.show();
  });
  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

function parseDialogParams(raw) {
  try {
    var params = JSON.parse(raw || '{}');
    delete params.browserWindow;
    return params;
  } catch (err) {
    return {};
  }
}

app.whenReady().then(() => {
  createWindow();

  app.on('web-contents-created', (event, contents) => {
    if (typeof contents.setWindowOpenHandler === 'function') {
      contents.setWindowOpenHandler(({ url }) => {
        shell.openExternal(url);
        return { action: 'deny' };
      });
    } else {
      contents.on('new-window', (windowEvent, url) => {
        windowEvent.preventDefault();
        shell.openExternal(url);
      });
    }

    contents.on('will-navigate', (navEvent, url) => {
      var parsed = new URL(url);
      if (parsed.protocol !== 'file:') {
        navEvent.preventDefault();
        shell.openExternal(url);
      }
    });
  });
});

app.on('second-instance', (event, commandLine) => {
  const projectFile = getProjectPathFromArgs(commandLine);

  if (mainWindow) {
    if (mainWindow.isMinimized()) mainWindow.restore();
    mainWindow.focus();
  }

  openProjectFile(projectFile);
});

app.on('open-file', (event, filePath) => {
  event.preventDefault();
  openProjectFile(filePath);
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});

ipcMain.on('get-app-path', (event) => {
  event.returnValue = app.getAppPath();
});

ipcMain.on('get-pending-project-file', (event) => {
  event.returnValue = pendingProjectFile;
  pendingProjectFile = null;
});

ipcMain.on('help', () => {
  shell.openExternal(HELP_URL);
});

ipcMain.on('dialog-open', (event, arg) => {
  event.returnValue = dialog.showOpenDialogSync(mainWindow, parseDialogParams(arg));
});

ipcMain.on('dialog-save', (event, arg) => {
  event.returnValue = dialog.showSaveDialogSync(mainWindow, parseDialogParams(arg));
});

ipcMain.on('app-quit', () => {
  if (mainWindow) mainWindow.close();
  app.quit();
});

ipcMain.on('app-minimize', () => {
  if (mainWindow) mainWindow.minimize();
});

ipcMain.on('app-maximize', () => {
  if (!mainWindow) return;
  if (mainWindow.isMaximized()) {
    mainWindow.restore();
  } else {
    mainWindow.maximize();
  }
});

ipcMain.on('devtools', () => {
  if (mainWindow) mainWindow.webContents.openDevTools();
});

ipcMain.on('win-set-position', (event, arg) => {
  if (!mainWindow || !arg) return;
  mainWindow.setPosition(arg.x, arg.y);
});
