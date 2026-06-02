var app_actions = {};
var app_events = {};
var debug_text = '';
var app_vars = {
    mdown: false,
    mx: 0,
    my: 0
};
var pendingProjectOpenPath = null;

var fs = window.ignisElectron.fs;
var path = window.ignisElectron.path;
const electronApi = window.ignisElectron || {
};
window.electronApi = electronApi;

$(document).ready(app_init);
$(window).on('load', app_init_late);

function dialog_open(arg)
{
    return electronApi.dialogOpen(arg);
}

function dialog_save(arg)
{
    return electronApi.dialogSave(arg);
}

function app_init_late()
{
    ignis_init_late();
    app_loading(false);
}

function app_register_event(event_name, callback)
{
    if (!app_events[event_name]) {
        app_events[event_name] = [];
    } 
    
    app_events[event_name].push(callback);
}

function app_execute_event(event_name, param)
{
    if (app_events[event_name]) {
        for (var i in app_events[event_name]) {
            app_events[event_name][i](param);
        }
    }
}

function app_loading(state)
{
    if (state) {
        $('#loading-overlay').show();
    } else {
        $('#loading-overlay').hide();
    }
}

function app_init()
{
    setTimeout(app_init_ex, 100);
}

function app_init_ex()
{
    $('#window-title').html(config.titleHTML + ' ' + config.versionHTML);
    window.title = config.title + ' v' + config.version;

    $(document).on('click', '[action]', function (e) {
        e.preventDefault();
        var id = $(this).attr('action');
        var param = $(this).attr('param');

        if (app_actions[id] != undefined) app_actions[id](param);
    });
    $('[event]').on('click', function (e) {
        e.preventDefault();
        var id = $(this).attr('event');
        var param = $(this).attr('param');

        app_execute_event(id, param);
    });
    $('[dblaction]').on('dblclick', function (e) {
        e.preventDefault();
        var id = $(this).attr('dblaction');
        var param = $(this).attr('param');

        if (app_actions[id] != undefined) app_actions[id](param);
    });
    $(document).on('keydown', app_debug_listen);

    app_register_action('exit', app_exit);
    app_register_action('minimize', app_minimize);
    app_register_action('maximize', app_maximize);
    app_register_action('devtools', app_devtools);
    app_register_action('reload', app_reload);
    app_register_action('chrome_gpu', app_gpu);
    app_register_action('chrome_flags', app_flags);

    if ($('body').hasClass('crapple')) {
        $('#window-title').css('left', ($('#top-menu').width() + 85) + 'px');
    } else {
        $('#window-title').css('left', ($('#top-menu').width() + 29) + 'px');
    }

    $('#window-title').on('mousedown', app_wmove_mdown);
    $(document).on('mousemove', app_wmove_move);
    $(document).on('mouseup', app_wmove_mup);

    $(window).on('resize', app_resize);

    ignis_init();

    if (electronApi.onOpenProject) {
        electronApi.onOpenProject(function (filePath) {
            open_project_file(filePath);
        });
    }

    if (electronApi.getPendingProjectFile) {
        var pendingFile = electronApi.getPendingProjectFile();
        if (pendingFile) {
            open_project_file(pendingFile);
        }
    }

    if (pendingProjectOpenPath) {
        open_project_file(pendingProjectOpenPath);
        pendingProjectOpenPath = null;
    }

    $('#vertical-splitter').on('mousedown', vs_mdown);
    $(document).on('mousemove', vs_mmove);
    $(document).on('mouseup', vs_mup);
}

var vs_y = 0;
var vs_h = 0;
var vs_md = false;

function vs_mdown(e)
{
    vs_md = true;
    vs_y = e.pageY;
    vs_h = $('#timeline-panel').height();
    vs_resize();
}

function vs_mup(e)
{
    vs_mmove(e);
    vs_md = false;
    vs_resize();
}

function vs_mmove(e)
{
    if (vs_md) {
        var d = vs_y - e.pageY;
        var h = vs_h + d;
        var mh = Math.round(window.innerHeight * 0.8);
        if (h < 245) h = 245;
        if (h > mh) h = mh;
        $('#timeline-panel').css('height', h + 'px');
        $('#content-panel').css('bottom', h + 'px');
        $('#vertical-splitter').css('bottom', h + 'px');
        $('#timeline-timeline').css('height', (h - 40) + 'px');
        $('#timeline-canvas').css('height', (h - 40) + 'px');
        $('#timeline-scroll').css('height', (h - 40) + 'px');
        $('#timeline-left-panel').css('height', h + 'px');
        vs_resize();
    }
}

function vs_resize()
{
    if (ignis) {
        ignis.preview.resize();
        ignis.timeline.resize();
    }
    app_resize();
}

function app_debug_listen(e)
{
    var key = e.key || String.fromCharCode(e.which || e.keyCode || 0);
    if (!key || key.length != 1) return;
    debug_text += key.toLowerCase();
    if (debug_text.length > 16) debug_text = debug_text.substr(debug_text.length - 16);
    if (debug_text.indexOf("debug") >= 0) {
        $('#debug-menu').show();
        if ($('body').hasClass('crapple')) {
            $('#window-title').css('left', ($('#top-menu').width() + 85) + 'px');
        } else {
            $('#window-title').css('left', ($('#top-menu').width() + 29) + 'px');
        }
        debug_text = '';
    }
}

function app_resize()
{
    if ($('body').hasClass('crapple')) {
        $('#window-title').css('left', ($('#top-menu').width() + 85) + 'px');
    } else {
        $('#window-title').css('left', ($('#top-menu').width() + 29) + 'px');
    }
}

function app_gpu()
{
    window.location = `chrome://gpu`;
}

function app_flags()
{
    window.location = `chrome://flags`;
}

function app_reload()
{
    window.location.reload();
}

function app_devtools()
{
    electronApi.devtools();
}

function open_project_file(filePath)
{
    if (!filePath) return;

    if (window.ignis && window.ignis.project) {
        window.ignis.project.load(filePath);
        return;
    }

    pendingProjectOpenPath = filePath;
}

function app_wmove_mdown(e)
{
    app_vars.mx = e.pageX;
    app_vars.my = e.pageY;
    app_vars.mdown = true;
}

function app_wmove_move(e)
{
    if (!app_vars.mdown) return;
    electronApi.setWindowPosition({ x: e.screenX - app_vars.mx, y: e.screenY - app_vars.my });
}

function app_wmove_mup(e)
{
    app_vars.mdown = false;
}

function app_register_action(name, callback)
{
    app_actions[name] = callback;
}

function app_exit()
{
    electronApi.quit();
}

function app_minimize()
{
    electronApi.minimize();
}

function app_maximize()
{
    electronApi.maximize();
}
