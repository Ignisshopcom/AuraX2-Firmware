var ignis = {
    timeline:   null,
    audio:      null,
    project:    null,
    config:     null,
    preview:    null,
    library:    null,
    resizer:    null,
    userconf:   null,
}

var ignis_cache = {};

var ignis_audio = new IgnisAudio(ignis);
var ignis_timeline = new IgnisTimeline(ignis);
var ignis_preview = new IgnisPreview(ignis);
var ignis_library = new IgnisLibrary(ignis);
var ignis_project = new IgnisProject(ignis);
var ignis_properties = new IgnisProperties(ignis);
var ignis_resizer = new IgnisResizer(ignis);
var ignis_userconf = new IgnisUserconf(ignis);

var ignis_prompt_callback = null;

function ignis_prompt(question, default_value, callback)
{
    ignis_prompt_callback = callback;
    $('#prompt-text').text(question);
    $('#prompt-input').val(default_value);
    $('#prompt-overlay').fadeIn();
    $('#prompt-input').focus();
    $('#prompt-input').select();
}

function ignis_prompt_init()
{
    $('#prompt-cancel').on('click', function () {
        $('#prompt-overlay').fadeOut();
        $('#prompt-input').blur();
    });
    $('#prompt-accept').on('click', function () {
        $('#prompt-overlay').fadeOut();
        $('#prompt-input').blur();
        var val = $('#prompt-input').val();
        if (ignis_prompt_callback) ignis_prompt_callback(val);
    });
    $('#prompt-input').on('keydown', function (e) {
        if (e.keyCode == 13) {
            $('#prompt-overlay').fadeOut();
            $('#prompt-input').blur();
            var val = $('#prompt-input').val();
            if (ignis_prompt_callback) ignis_prompt_callback(val);
        }
        if (e.keyCode == 27) {
            $('#prompt-overlay').fadeOut();
        $('#prompt-input').blur();
        }
    });
}

function ignis_init_late()
{
    ignis_preview.init();
    IgnisSliderInit();
    $('#startup-overlay').fadeOut(400, $.proxy(function () {
        $('#startup-overlay').remove();
    }, this));
}

function ignis_init()
{
    ignis_userconf.init();
    ignis_resizer.init();
    ignis_timeline.init();
    ignis_library.init();
    ignis_project.init();
    ignis_properties.init();    
    ignis_audio.init();
    //ignis_audio.loadFile('test.mp3');

    app_register_action('help', ignis_help);

    ignis_prompt_init();

    if (window.electronApi.platform == 'darwin') {
        config.magick_darwin = window.electronApi.resourcesPath + path.sep + 'app' + path.sep + 'magick';
        $('body').addClass('crapple');
    }
}

function ignis_help()
{
    window.electronApi.help();    
}

function ignis_appdir()
{
    if (ignis_cache.appdir) return ignis_cache.appdir;
    var homedir = window.electronApi.homeDir;
    var appdir = homedir + path.sep + '.ignis-studio';

    if (window.electronApi.platform == 'win32') {
        appdir = window.electronApi.appDataPath + path.sep + 'ignis-studio';
    }

    if (!fs.existsSync(appdir)) {
        fs.mkdirSync(appdir, { recursive: true });
    }

    ignis_cache.appdir = appdir;

    return appdir;
}

function ignis_thumbdir() {
    if (ignis_cache.thumbdir) return ignis_cache.thumbdir;
    
    var thumbdir = ignis_appdir() + path.sep + 'thumbs';

    if (!fs.existsSync(thumbdir)) {
        fs.mkdirSync(thumbdir, { recursive: true });
    }

    ignis_cache.thumbdir = thumbdir;

    return thumbdir;
}

function ignis_texdir() {
    if (ignis_cache.texdir) return ignis_cache.texdir;

    var thumbdir = ignis_appdir() + path.sep + 'tex';

    if (!fs.existsSync(thumbdir)) {
        fs.mkdirSync(thumbdir, { recursive: true });
    }

    ignis_cache.texdir = thumbdir;

    return thumbdir;
}

function ignis_dir(dir) {
    if (ignis_cache[dir]) return ignis_cache[dir];

    var thumbdir = ignis_appdir() + path.sep + dir;

    if (!fs.existsSync(thumbdir)) {
        fs.mkdirSync(thumbdir, { recursive: true });
    }

    ignis_cache[dir] = thumbdir;

    return thumbdir;
}
