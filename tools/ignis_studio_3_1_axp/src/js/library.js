function IgnisLibrary(ignis)
{
    this.ignis = ignis;
    ignis.library = this;
    this.elCache = {};

    this.drag_active = false;
    this.drag_item = null;
    this.selected_item = null;
    this.selected_items = [];
    this.selected_audio = [];
    this.library = {
        images: [],
        audio: []
    };
    this.effects = [
        { id: 1, name: 'Solid', icon: 'fa-square', colors: ['#ff6000'], speed: null, intensity: null, size: null },
        { id: 2, name: 'Android', icon: 'fa-android', colors: ['#ff6000'], speed: 'Speed', intensity: null, size: 'Width', sizeMax: 32 },
        { id: 10, name: 'BPM', icon: 'fa-heartbeat', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'BPM', intensity: 'Beat depth', size: null },
        { id: 11, name: 'Flow', icon: 'fa-water', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Waves', size: null },
        { id: 12, name: 'Gravcenter', icon: 'fa-dot-circle', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: 'Width', sizeMax: 32 },
        { id: 13, name: 'Gravfreq', icon: 'fa-wave-square', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Frequency', size: null },
        { id: 14, name: 'Chase 2', icon: 'fa-forward', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: 'Tail', sizeMax: 32 },
        { id: 15, name: 'Chase 3', icon: 'fa-forward', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: 'Tail', sizeMax: 32 },
        { id: 16, name: 'Chunchun', icon: 'fa-asterisk', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: 'Width', sizeMax: 32 },
        { id: 17, name: 'Lake', icon: 'fa-water', colors: ['#001050', '#00aaff', '#78ffd2'], speed: 'Speed', intensity: 'Wave depth', size: null },
        { id: 18, name: 'Meteor', icon: 'fa-meteor', colors: ['#ffffff', '#00b4ff', '#ff6000'], speed: 'Speed', intensity: 'Trail fade', size: 'Tail', sizeMax: 40 },
        { id: 19, name: 'Noise 3', icon: 'fa-random', colors: ['#ff4081', '#00e676', '#40c4ff'], speed: 'Speed', intensity: 'Scale', size: null },
        { id: 20, name: 'Oscillate', icon: 'fa-exchange-alt', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: 'Width', sizeMax: 32 },
        { id: 21, name: 'Ripple', icon: 'fa-dot-circle', colors: ['#ffffff', '#7c4dff', '#00b4ff'], speed: 'Speed', intensity: null, size: 'Width', sizeMax: 32 },
        { id: 22, name: 'Running', icon: 'fa-running', colors: ['#00e5ff', '#ffffff', '#ff6000'], speed: 'Speed', intensity: 'Density', size: null },
        { id: 23, name: 'Strobe', icon: 'fa-bolt', colors: ['#ffffff'], speed: 'Rate', intensity: 'Duty', size: null },
        { id: 24, name: 'Fade', icon: 'fa-adjust', colors: ['#ff3b30', '#1e88e5', '#ffffff'], speed: 'Speed', intensity: 'Depth', size: null },
        { id: 25, name: 'Rainbow', icon: 'fa-rainbow', colors: ['#ff0033', '#ffe600', '#00e676'], speed: 'Speed', intensity: 'Spread', size: null },
        { id: 26, name: 'Twinkle', icon: 'fa-star', colors: ['#ffffff', '#7c4dff', '#00e5ff'], speed: 'Speed', intensity: 'Density', size: null },
        { id: 27, name: 'Sparkle', icon: 'fa-sparkles', colors: ['#ffffff', '#ffb000', '#00d5ff'], speed: 'Speed', intensity: 'Amount', size: null },
        { id: 28, name: 'Fireworks', icon: 'fa-burst', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Bursts', size: 'Width', sizeMax: 24 },
        { id: 29, name: 'Scanner', icon: 'fa-arrows-alt-h', colors: ['#ff0033', '#ffffff', '#00b4ff'], speed: 'Speed', intensity: 'Trail', size: 'Width', sizeMax: 32 },
        { id: 30, name: 'Dual Scanner', icon: 'fa-exchange-alt', colors: ['#ff0033', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Trail', size: 'Width', sizeMax: 32 },
        { id: 31, name: 'Theater Chase', icon: 'fa-ellipsis-h', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Spacing', size: 'Duty', sizeMax: 12 },
        { id: 32, name: 'Color Wipe', icon: 'fa-fill-drip', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: null },
        { id: 33, name: 'Juggle', icon: 'fa-circle-notch', colors: ['#ff0033', '#00e676', '#40c4ff'], speed: 'Speed', intensity: 'Dots', size: null },
        { id: 34, name: 'Sinelon', icon: 'fa-compress-arrows-alt', colors: ['#ff0033', '#ffffff', '#00b4ff'], speed: 'Speed', intensity: 'Trail', size: 'Width', sizeMax: 24 },
        { id: 35, name: 'Fire Flicker', icon: 'fa-fire', colors: ['#000000', '#ff4800', '#ffe678'], speed: 'Speed', intensity: 'Heat', size: null },
        { id: 36, name: 'Plasma', icon: 'fa-atom', colors: ['#ff00dc', '#00ffff', '#82ff00'], speed: 'Speed', intensity: 'Scale', size: null },
        { id: 37, name: 'Gradient', icon: 'fa-grip-lines', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Spread', size: null },
        { id: 38, name: 'Breath', icon: 'fa-adjust', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Floor', size: null },
        { id: 39, name: 'Dots', icon: 'fa-braille', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Count', size: 'Width', sizeMax: 16 },
        { id: 40, name: 'Counter Chase', icon: 'fa-random', colors: ['#ff3b30', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: 'Tail', sizeMax: 32 },
        { id: 41, name: 'Split Chase', icon: 'fa-compress-arrows-alt', colors: ['#ff6000', '#00e676', '#40c4ff'], speed: 'Speed', intensity: null, size: 'Tail', sizeMax: 32 },
        { id: 42, name: 'Collision', icon: 'fa-arrows-alt-h', colors: ['#ff0033', '#ffffff', '#00b4ff'], speed: 'Speed', intensity: null, size: 'Width', sizeMax: 32 },
        { id: 43, name: 'Saw', icon: 'fa-signal', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Frequency', size: null },
        { id: 44, name: 'Chevron', icon: 'fa-angle-double-right', colors: ['#00ffd2', '#0050ff', '#ff00dc'], speed: 'Speed', intensity: 'Spacing', size: null },
        { id: 45, name: 'Pulse Train', icon: 'fa-ellipsis-h', colors: ['#ffffff', '#ff6000', '#00b4ff'], speed: 'Speed', intensity: 'Pulses', size: 'Width', sizeMax: 24 },
        { id: 46, name: 'Cross Waves', icon: 'fa-wave-square', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Frequency', size: null },
        { id: 47, name: 'Barber Pole', icon: 'fa-candy-cane', colors: ['#ff0000', '#ffffff', '#0046ff'], speed: 'Speed', intensity: 'Bands', size: null },
        { id: 48, name: 'Scan Bars', icon: 'fa-bars', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Bars', size: 'Width', sizeMax: 16 },
        { id: 49, name: 'Prism', icon: 'fa-gem', colors: ['#ff00dc', '#00ffff', '#82ff00'], speed: 'Speed', intensity: 'Spread', size: null },
        { id: 50, name: 'Spin', icon: 'fa-circle-notch', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: null, size: 'Width', sizeMax: 24 },
        { id: 51, name: 'Twist', icon: 'fa-dna', colors: ['#7800ff', '#00ff64', '#ffdc00'], speed: 'Speed', intensity: 'Density', size: null },
        { id: 52, name: 'Chase', icon: 'fa-forward', colors: ['#ff6000', '#00b4ff', '#ffffff'], speed: 'Speed', intensity: 'Accent sparks', size: 'Tail', sizeMax: 32 },
        { id: 53, name: 'Fire', icon: 'fa-fire', colors: ['#000000', '#ff4800', '#ffe678'], speed: 'Speed', intensity: 'Flame scale', size: null }
    ];
    this.effectAliases = {
        solid: 1,
        android: 2,
        bpm: 10,
        flow: 11,
        gravcenter: 12,
        gravfreq: 13,
        chase: 14,
        chase2: 14,
        chase3: 15,
        chunchun: 16,
        lake: 17,
        meteor: 18,
        noise: 19,
        noise3: 19,
        oscillate: 20,
        ripple: 21,
        running: 22,
        strobe: 23,
        fade: 24,
        rainbow: 25,
        twinkle: 26,
        sparkle: 27,
        fireworks: 28,
        scanner: 29,
        scannerdual: 30,
        dualscanner: 30,
        theater: 31,
        theaterchase: 31,
        colorwipe: 32,
        wipe: 32,
        juggle: 33,
        sinelon: 34,
        fire: 35,
        fireflicker: 35,
        plasma: 36,
        gradient: 37,
        breath: 38,
        dots: 39,
        counterchase: 40,
        splitchase: 41,
        collision: 42,
        collide: 42,
        saw: 43,
        chevron: 44,
        pulsetrain: 45,
        crosswaves: 46,
        barberpole: 47,
        scanbars: 48,
        prism: 49,
        spin: 50,
        twist: 51,
        chase1: 52,
        chaseclassic: 52,
        fireclassic: 53
    };
    this.effectPalettes = [
        { id: 0, name: 'Custom', colors: ['#ff6000', '#00b4ff', '#ffffff'] },
        { id: 1, name: 'Rainbow', colors: ['#ff0000', '#00ff00', '#0000ff'] },
        { id: 2, name: 'Fire', colors: ['#000000', '#ff4800', '#ffe678'] },
        { id: 3, name: 'Ocean', colors: ['#000c50', '#00aaff', '#78ffdc'] },
        { id: 4, name: 'Forest', colors: ['#001e00', '#008c24', '#dcff50'] },
        { id: 5, name: 'Party', colors: ['#ff0050', '#00dcff', '#ffdc00'] },
        { id: 6, name: 'Sunset', colors: ['#3c0050', '#ff4800', '#ffbe46'] },
        { id: 7, name: 'Polar', colors: ['#001850', '#2bd9ff', '#ffffff'] },
        { id: 8, name: 'Lava', colors: ['#000000', '#a00000', '#ff4600', '#ffd66e'] },
        { id: 9, name: 'Pastel', colors: ['#ff84c0', '#7cffbe', '#84beff'] },
        { id: 10, name: 'Neon', colors: ['#ff00dc', '#00ffff', '#82ff00'] },
        { id: 11, name: 'Candy', colors: ['#ff286e', '#ffffff', '#50d2ff'] },
        { id: 12, name: 'Aurora', colors: ['#181064', '#00dcaa', '#a050ff'] },
        { id: 13, name: 'Vintage', colors: ['#50140a', '#dc7824', '#145a5f'] },
        { id: 14, name: 'Rainbow Stripe', colors: ['#ff0000', '#ffff00', '#00ff00', '#0000ff'] },
        { id: 15, name: 'Blue Purple', colors: ['#000c50', '#0078ff', '#9600ff', '#ff28d2'] },
        { id: 16, name: 'Pink Candy', colors: ['#ff005c', '#ffb4dc', '#ffffff', '#78dcff'] },
        { id: 17, name: 'C9', colors: ['#ff0000', '#ffa000', '#00b446', '#0050ff'] },
        { id: 18, name: 'Tiamat', colors: ['#120046', '#00b4be', '#ff2878', '#ffb428'] },
        { id: 19, name: 'Dry Wet', colors: ['#ffa046', '#ffe696', '#28b4ff', '#001e78'] },
        { id: 20, name: 'Red Blue', colors: ['#ff0000', '#0046ff', '#ff0000'] },
        { id: 21, name: 'Yellow Green', colors: ['#ffdc00', '#00ff46', '#ffdc00'] },
        { id: 22, name: 'Purple Green', colors: ['#7800ff', '#00ff64', '#7800ff'] },
        { id: 23, name: 'Warm White', colors: ['#ff7828', '#ffebb4', '#ff7828'] },
        { id: 24, name: 'Aqua Magenta', colors: ['#00ffd2', '#0050ff', '#ff00dc', '#ffffff'] },
        { id: 25, name: 'Police', colors: ['#ff0000', '#ffffff', '#0050ff'] },
        { id: 26, name: 'Matrix', colors: ['#001400', '#00ff46', '#baff80'] },
        { id: 27, name: 'Sakura', colors: ['#ff2d85', '#ffd2e8', '#ffffff'] },
        { id: 28, name: 'Electric', colors: ['#0014ff', '#00f0ff', '#ffffff'] },
        { id: 29, name: 'Amber Teal', colors: ['#ff8a00', '#ffe06e', '#00b4aa'] }
    ];
    this.effectDraft = {
        type: 'effect',
        hash: 'effect:1',
        id: 1,
        name: 'Solid',
        speed: 500,
        intensity: 128,
        size: 3,
        paletteId: 0,
        colors: ['#ff6000', '#00b4ff', '#ffffff']
    };
    this.effectColorIndex = 0;
    this.effectPreviewTimer = null;
    this.effectTimelineTimer = null;
    this.effectGalleryPreviewCache = {};
    this.lib_image_dir = '';
    this.lib_audio_dir = '';

    this.resize_queue = 0;

    this.dialog_import_properties = {
        title: 'Import Assets',
        properties: ['openFile', 'multiSelections'],
        filters: [
            { name: 'Images & Music', extensions: ['jpg', 'png', 'gif', 'bmp', 'mp3', 'ogg'] },
            { name: 'Images', extensions: ['jpg', 'png', 'gif', 'bmp'] },
            { name: 'Music', extensions: ['mp3', 'ogg'] },
            { name: 'All Files', extensions: ['*'] }
        ],
    };
}

IgnisLibrary.prototype.check = function ()
{
    var reload = false;

    // remove missing images from library
    var ir = [];
    for (var i in this.library.images) {
        var n = this.library.images[i];
        if (!fs.existsSync(n.path)) {
            ir.push(i);
        }
    }
    ir.reverse();
    for (var i of ir) {
        this.library.images.splice(i, 1);
    }

    // replace old image entries
    var tr = [];
    for (var i in this.library.images) {
        var n = this.library.images[i];
        var tp = n.path.substring(0, this.lib_image_dir.length);
        // is local -> continue
        if (tp == this.lib_image_dir) continue;
        
        // is non-local -> reimport
        this.addFile(n.path);
        tr.push(n.hash);
        reload = true;
    }
    for (var hash of tr) {
        this.removeImage(hash, true);
    }

    // add not tracked images to library
    var files = fs.readdirSync(this.lib_image_dir);
    for (var fname of files) {
        var filepath = this.lib_image_dir + path.sep + fname;
        var hash = this.md5file(filepath);
        if (!this.imageHashExists(hash)) {
            this.addFile(filepath, true);
        }
    }

    // remove missing audio from library
    var ar = [];
    for (var i in this.library.audio) {
        var n = this.library.audio[i];
        if (!fs.existsSync(n.path)) {
            ar.push(i);
        }
    }
    ar.reverse();
    for (var i of ar) {
        this.library.audio.splice(i, 1);
    }

    // replace old audio entries
    var atr = [];
    for (var i in this.library.audio) {
        var n = this.library.audio[i];
        var tp = n.path.substring(0, this.lib_audio_dir.length);
        // is local -> continue
        if (tp == this.lib_audio_dir) continue;
        
        // is non-local -> reimport
        this.addFile(n.path);
        atr.push(n.hash);
        reload = true;
    }
    for (var hash of atr) {
        this.removeAudio(hash, true);
    }

    // add not tracked images to library
    var files = fs.readdirSync(this.lib_audio_dir);
    for (var fname of files) {
        var filepath = this.lib_audio_dir + path.sep + fname;
        var hash = this.md5file(filepath);
        if (!this.audioHashExists(hash)) {
            this.addFile(filepath, true);
        }
    }

    this.save();

    if (reload) {
        app_reload();
    }
}

IgnisLibrary.prototype.audioHashExists = function (hash)
{
    for (var i in this.library.audio) {
        var n = this.library.audio[i];
        if (n.hash == hash) return true;
    }
    return false;
}

IgnisLibrary.prototype.imageHashExists = function (hash)
{
    for (var i in this.library.images) {
        var n = this.library.images[i];
        if (n.hash == hash) return true;
    }
    return false;
}

IgnisLibrary.prototype.init = function ()
{
    this.lib_image_dir = ignis_dir('images');
    this.lib_audio_dir = ignis_dir('audio');

    ignis_cache.has_thumb = {};

    if (this.load()) {
        this.check();
        this.genSizes();
        this.genThumbs();
        this.fetchAudioInfo();
        this.update();
    } else {
        this.save();
        this.check();
    }

    //this.clean(true);

    this.drag();

    app_register_action('gen_thumbs', $.proxy(this.genThumbs, this));

    $(window).on('resize', $.proxy(this.resize, this));
    this.resize();

    app_register_action('library_images', $.proxy(function () { this.switch('images') }, this));
    app_register_action('library_effects', $.proxy(function () { this.switch('effects') }, this));
    app_register_action('library_audio', $.proxy(function () { this.switch('audio') }, this));
    app_register_action('library_import', $.proxy(this.importFiles, this));
    app_register_action('library_clean', $.proxy(this.clean, this));
    app_register_action('library_delete_all_images', $.proxy(this.removeAllImages, this));
    
    app_register_event('project_leds_updated', $.proxy(this.genTexs, this));

    setInterval($.proxy(this.imageUpdate, this), 1000);

    $(document).on('keydown', $.proxy(this.onKeyDown, this));
}

IgnisLibrary.prototype.hideGeneratedImages = function ()
{
    for (var i in this.library.images) {
        var n = this.library.images[i];
        if (n.generated) {
            $('#library-images').find('[hash=' + n.hash + ']').remove();
        }
    }
}

IgnisLibrary.prototype.onKeyDown = function (e)
{
    if (e.keyCode == 46 && this.selected_items && this.selected_items.length > 0 && $('#library-images').is(':visible') && $('.library-img.selected').length > 0) {
        if (confirm("Are you sure you want to delete selected images from library?")) {
            this.removeSelectedImages();
        }
    }
}

IgnisLibrary.prototype.clean = function (noreload)
{
    // resizer
    var dir = ignis_dir('resizer');
    var files = fs.readdirSync(dir);
    for (var f of files) {
        f = dir + path.sep + f;
        fs.unlinkSync(f);
    }
    // thumbs
    var dir = ignis_thumbdir();
    var files = fs.readdirSync(dir);
    for (var f of files) {
        f = dir + path.sep + f;
        fs.unlinkSync(f);
    }
    // textures
    var dir = ignis_texdir();
    var files = fs.readdirSync(dir);
    for (var f of files) {
        f = dir + path.sep + f;
        fs.unlinkSync(f);
    }
    // clean library
    var real_images = [];
    for (var i in this.library.images) {
        var n = this.library.images[i];
        if (fs.existsSync(n.path)) {
            n.tex = n.thumb = false;
            real_images.push(n);
        }
    }
    this.library.images = real_images;
    this.save();
    // reload app
    if (!noreload)
        window.location.reload();
}

IgnisLibrary.prototype.imageUpdate = function ()
{
    var thumbdir = ignis_thumbdir();
    var thmb = [];
    var that = this;
    $('#library-images').find('.loading').each(function () {
        var el = $(this);
        var hash = el.attr('hash');
        var n = that.getImageByHash(hash);
        if (!n || n.generated) {
            el.remove();
            return;
        }
        var r = (n.resolution.r ? n.resolution.r : 1);
        var tpath = thumbdir + path.sep + hash + '.jpg';
        tpath = tpath.split(path.sep).join('/');
        if (fs.existsSync(tpath)) {
            el.removeClass('loading');
            el.css('background-image', 'url(' + tpath + '?t=' + (new Date()).getMilliseconds() + ')');
            if (r > 1) {
                el.css('background-repeat', 'no-repeat');
            }
            that.elCache[hash] = el;
            thmb.push(hash);
        }
    });
    for (var hash of thmb) {
        this.library.images[this.getImageIndexByHash(hash)].thumb = true;
        ignis_cache.has_thumb[hash] = true;
    }
    this.hideGeneratedImages();

    return;
    var thumbdir = ignis_thumbdir();

    for (var i in this.library.images) {
        var n = this.library.images[i];
        if (!n.type) n.type = 'image';

        //var el = (this.elCache[n.hash] ? this.elCache[n.hash] : $('#library-images').find('[hash=' + n.hash + ']'));
        var el = $('#library-images').find('[hash=' + n.hash + ']');
        if (el.length == 0) continue;

        if (ignis_cache.has_thumb[n.hash] && el.hasClass('loading')) {
            el.removeClass('loading');
        }
        if (ignis_cache.has_thumb[n.hash]) continue;

        var tpath = thumbdir + path.sep + n.hash + '.jpg';
        tpath = tpath.split(path.sep).join('/');

        if (fs.existsSync(tpath)) {
            //var stats = fs.statSync(n.path);
            n.thumb = true;
        } else {
            n.thumb = false;
        }

        if (!this.elCache[n.hash]) this.elCache[n.hash] = el;

        if (el.hasClass('loading')) {
            if (n.thumb) {
                el.removeClass('loading');
                el.css('background-image', 'url(' + tpath + '?t=' + (new Date()).getMilliseconds() + ')');
                ignis_cache.has_thumb[n.hash] = true;
            }
        }
    }
}

IgnisLibrary.prototype.md5file = function (fileName)
{
    var buff = fs.readFileSync(fileName);
    return window.electronApi.md5(buff);
}

IgnisLibrary.prototype.fileUrl = function (filePath)
{
    var url = filePath.split(path.sep).join('/');
    if (/^[a-zA-Z]:/.test(url)) url = '/' + url;
    return 'file://' + encodeURI(url).replace(/#/g, '%23').replace(/\?/g, '%3F');
}

IgnisLibrary.prototype.getImportType = function (filePath)
{
    var ext = path.extname(filePath).toLowerCase().substring(1);
    if (['jpg', 'jpeg', 'png', 'bmp', 'gif'].indexOf(ext) >= 0) return 'image';
    if (['mp3', 'ogg'].indexOf(ext) >= 0) return 'audio';
    return false;
}

IgnisLibrary.prototype.importError = function (filePath)
{
    alert('This file cannot be imported: ' + path.basename(filePath || 'unknown file'));
}

IgnisLibrary.prototype.validateImageImport = function (filePath, callback)
{
    var done = false;
    var img = new Image();
    var finish = function (ok) {
        if (done) return;
        done = true;
        callback(ok);
    };

    img.onload = function () {
        finish(img.width > 0 && img.height > 0);
    };
    img.onerror = function () {
        finish(false);
    };

    setTimeout(function () {
        finish(false);
    }, 15000);

    img.src = this.fileUrl(filePath);
}

IgnisLibrary.prototype.validateAudioImport = function (filePath, callback)
{
    var done = false;
    var audio = new Audio();
    var finish = function (ok) {
        if (done) return;
        done = true;
        callback(ok);
    };

    audio.preload = 'metadata';
    audio.onloadedmetadata = function () {
        finish(isFinite(audio.duration) && audio.duration > 0);
    };
    audio.onerror = function () {
        finish(false);
    };

    setTimeout(function () {
        finish(false);
    }, 15000);

    audio.src = this.fileUrl(filePath);
}

IgnisLibrary.prototype.importFile = function (filePath, callback)
{
    var finish = $.proxy(function (ok) {
        if (callback) callback(ok);
    }, this);

    try {
        if (!filePath || !fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
            this.importError(filePath);
            return finish(false);
        }

        var type = this.getImportType(filePath);
        if (!type) {
            this.importError(filePath);
            return finish(false);
        }

        var validator = type == 'audio' ? this.validateAudioImport : this.validateImageImport;
        validator.call(this, filePath, $.proxy(function (valid) {
            if (!valid) {
                this.importError(filePath);
                return finish(false);
            }

            try {
                finish(!!this.addFile(filePath));
            } catch (ex) {
                console.error('Import failed:', filePath, ex);
                this.importError(filePath);
                finish(false);
            }
        }, this));
    } catch (ex) {
        console.error('Import failed:', filePath, ex);
        this.importError(filePath);
        finish(false);
    }
}

IgnisLibrary.prototype.importFileList = function (filePaths)
{
    var pending = filePaths.length;
    var added = false;

    if (pending == 0) return;

    for (var i in filePaths) {
        this.importFile(filePaths[i], $.proxy(function (ok) {
            if (ok) added = true;
            pending--;
            if (pending == 0 && added) {
                this.genThumbs();
                this.update();
            }
        }, this));
    }
}

IgnisLibrary.prototype.importFiles = function ()
{
    var filenames = dialog_open(this.dialog_import_properties);
    if (!filenames) return;

    this.importFileList(filenames);
}

IgnisLibrary.prototype.getAudioByHash = function (hash)
{
    for (var i in this.library.audio) {
        var n = this.library.audio[i];

        if (n.hash == hash) return n;
    }
    return false;
}

IgnisLibrary.prototype.getAudioByUid = function (uid)
{
    for (var i in this.library.audio) {
        var n = this.library.audio[i];

        if (n.uid == uid) return n;
    }
    return false;
}

IgnisLibrary.prototype.getImageIndexByHash = function (hash)
{
    for (var i in this.library.images) {
        if (this.library.images[i].hash == hash) return i;
    }
    return false;
}

IgnisLibrary.prototype.getImageByHash = function (hash)
{
    for (var i in this.library.images) {
        var n = this.library.images[i];

        if (n.hash == hash) return n;
    }
    return false;
}

IgnisLibrary.prototype.getEffectByHash = function (hash)
{
    var id = this.normalizeEffectId(hash);
    for (var i in this.effects) {
        var n = this.effects[i];
        if (n.id == id) return n;
    }
    return false;
}

IgnisLibrary.prototype.normalizeEffectId = function (value)
{
    value = String(value || '').replace(/^effect:/, '').toLowerCase();
    if (this.effectAliases[value] !== undefined) return this.effectAliases[value];
    var id = parseInt(value);
    if (isNaN(id)) return 1;
    return id;
}

IgnisLibrary.prototype.getEffectById = function (id)
{
    id = this.normalizeEffectId(id);
    for (var i in this.effects) {
        if (this.effects[i].id == id) return this.effects[i];
    }
    return this.effects[0];
}

IgnisLibrary.prototype.getEffectColorSlots = function (id)
{
    id = this.normalizeEffectId(id);
    if (id == 25) return 0;
    if (id == 1 || id == 23) return 1;
    if (id == 2) return 2;
    return 3;
}

IgnisLibrary.prototype.getEffectPalette = function (id)
{
    id = parseInt(id);
    if (isNaN(id)) id = 0;
    for (var i in this.effectPalettes) {
        if (this.effectPalettes[i].id == id) return this.effectPalettes[i];
    }
    return this.effectPalettes[0];
}

IgnisLibrary.prototype.normalizeEffectColor = function (value)
{
    value = String(value || '').trim();
    if (!value.match(/^#[0-9a-fA-F]{6}$/)) return '#ffffff';
    return value.toLowerCase();
}

IgnisLibrary.prototype.effectHexToRgb = function (hex)
{
    hex = this.normalizeEffectColor(hex).replace('#', '');
    var value = parseInt(hex, 16);
    return {
        r: (value >> 16) & 255,
        g: (value >> 8) & 255,
        b: value & 255
    };
}

IgnisLibrary.prototype.effectRgbToHex = function (rgb)
{
    var r = Math.max(0, Math.min(255, Math.round(rgb.r || 0)));
    var g = Math.max(0, Math.min(255, Math.round(rgb.g || 0)));
    var b = Math.max(0, Math.min(255, Math.round(rgb.b || 0)));
    return '#' + [r, g, b].map(function (v) {
        var s = v.toString(16);
        return s.length < 2 ? '0' + s : s;
    }).join('');
}

IgnisLibrary.prototype.effectRgbToHsv = function (rgb)
{
    var r = (rgb.r || 0) / 255;
    var g = (rgb.g || 0) / 255;
    var b = (rgb.b || 0) / 255;
    var max = Math.max(r, g, b);
    var min = Math.min(r, g, b);
    var d = max - min;
    var h = 0;

    if (d > 0) {
        if (max == r) h = ((g - b) / d) % 6;
        else if (max == g) h = ((b - r) / d) + 2;
        else h = ((r - g) / d) + 4;
        h *= 60;
        if (h < 0) h += 360;
    }

    return {
        h: h,
        s: max == 0 ? 0 : d / max,
        v: max
    };
}

IgnisLibrary.prototype.effectHsvToHex = function (hsv)
{
    var h = ((hsv.h || 0) % 360 + 360) % 360;
    var s = Math.max(0, Math.min(1, hsv.s || 0));
    var v = Math.max(0, Math.min(1, hsv.v || 0));
    var c = v * s;
    var x = c * (1 - Math.abs((h / 60) % 2 - 1));
    var m = v - c;
    var r = 0, g = 0, b = 0;

    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }

    return this.effectRgbToHex({
        r: (r + m) * 255,
        g: (g + m) * 255,
        b: (b + m) * 255
    });
}

IgnisLibrary.prototype.normalizeEffectDraft = function (draft)
{
    var effect = this.getEffectById(draft && draft.id);
    var palette = this.getEffectPalette((draft && draft.paletteId !== undefined) ? draft.paletteId : 0);
    var slots = this.getEffectColorSlots(effect.id);
    var colors = (draft && draft.colors && draft.colors.length ? draft.colors : effect.colors || palette.colors).slice(0);
    while (colors.length < Math.max(3, slots)) colors.push('#000000');
    colors = colors.map($.proxy(this.normalizeEffectColor, this));

    return {
        type: 'effect',
        hash: 'effect:' + effect.id,
        id: effect.id,
        name: effect.name,
        speed: this.clampEffectNumber((draft && draft.speed !== undefined) ? draft.speed : 100, 10, 1000),
        intensity: this.clampEffectNumber((draft && draft.intensity !== undefined) ? draft.intensity : 128, 0, 255),
        size: this.clampEffectNumber((draft && draft.size !== undefined) ? draft.size : 3, 1, effect.sizeMax || 40),
        paletteId: palette.id,
        colors: colors
    };
}

IgnisLibrary.prototype.clampEffectNumber = function (value, min, max)
{
    value = parseInt(value);
    if (isNaN(value)) value = min;
    if (value < min) value = min;
    if (value > max) value = max;
    return value;
}

IgnisLibrary.prototype.getEffectDraft = function ()
{
    this.effectDraft = this.normalizeEffectDraft(this.effectDraft);
    return $.extend(true, {}, this.effectDraft);
}

IgnisLibrary.prototype.getDefaultEffectDraft = function (effect)
{
    effect = this.getEffectById(effect && effect.id);
    var palette = (effect.id == 1) ? this.effectPalettes[0] : this.effectPalettes[1];
    return this.normalizeEffectDraft({
        id: effect.id,
        hash: 'effect:' + effect.id,
        name: effect.name,
        speed: 500,
        intensity: 128,
        size: effect.size ? Math.min(effect.sizeMax || 40, Math.max(1, effect.id == 18 ? 10 : 3)) : 3,
        paletteId: palette.id,
        colors: palette.colors.slice(0)
    });
}

IgnisLibrary.prototype.effectGalleryPreviewDataUrl = function (effect)
{
    effect = this.getEffectById(effect && effect.id);
    var cacheKey = 'effect-gallery-' + effect.id;
    if (this.effectGalleryPreviewCache[cacheKey]) return this.effectGalleryPreviewCache[cacheKey];

    var canvas = document.createElement('canvas');
    var width = 160;
    var height = 160;
    canvas.width = width;
    canvas.height = height;

    var ctx = canvas.getContext('2d');
    var colors = (effect.colors && effect.colors.length ? effect.colors : ['#ff6000', '#00b4ff', '#ffffff'])
        .map($.proxy(this.effectHexToRgb, this));

    function clamp(value, min, max) {
        return Math.max(min, Math.min(max, value));
    }

    function rgb(c, alpha) {
        alpha = alpha === undefined ? 1 : alpha;
        return 'rgba(' + Math.round(clamp(c.r || 0, 0, 255)) + ',' +
            Math.round(clamp(c.g || 0, 0, 255)) + ',' +
            Math.round(clamp(c.b || 0, 0, 255)) + ',' + alpha + ')';
    }

    function mix(a, b, t) {
        t = clamp(t, 0, 1);
        return {
            r: a.r + (b.r - a.r) * t,
            g: a.g + (b.g - a.g) * t,
            b: a.b + (b.b - a.b) * t
        };
    }

    function palette(t) {
        if (colors.length == 1) return colors[0];
        t = ((t % 1) + 1) % 1;
        var pos = t * (colors.length - 1);
        var idx = Math.floor(pos);
        return mix(colors[idx], colors[Math.min(colors.length - 1, idx + 1)], pos - idx);
    }

    function shade(amount) {
        ctx.fillStyle = 'rgba(0,0,0,' + amount + ')';
        ctx.fillRect(0, 0, width, height);
    }

    function vignette() {
        var g = ctx.createRadialGradient(width * 0.46, height * 0.42, 8, width * 0.5, height * 0.5, width * 0.8);
        g.addColorStop(0, 'rgba(255,255,255,0.08)');
        g.addColorStop(0.62, 'rgba(255,255,255,0.00)');
        g.addColorStop(1, 'rgba(0,0,0,0.58)');
        ctx.fillStyle = g;
        ctx.fillRect(0, 0, width, height);
    }

    function seedRand(seed) {
        seed = seed >>> 0;
        return function () {
            seed ^= seed << 13;
            seed ^= seed >>> 17;
            seed ^= seed << 5;
            return ((seed >>> 0) & 0xffff) / 0xffff;
        };
    }

    function drawPixelField(kind) {
        var image = ctx.createImageData(width, height);
        var rand = seedRand(0x7562 + effect.id * 97);
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                var nx = x / width;
                var ny = y / height;
                var value = 0;
                var p = 0;

                if (kind == 'rainbow') {
                    p = nx * 1.35 + ny * 0.55;
                    value = 0.98;
                } else if (kind == 'fire') {
                    p = 0.08 + (1 - ny) * 0.72 + Math.sin(nx * 22 + ny * 15) * 0.07;
                    value = clamp(1.2 - ny + Math.sin(nx * 28) * 0.08, 0, 1);
                } else if (kind == 'noise') {
                    var n = (Math.sin((x + effect.id) * 0.25) + Math.sin((y - effect.id) * 0.31) + Math.sin((x + y) * 0.17)) / 3;
                    p = n * 0.5 + 0.5;
                    value = 0.35 + p * 0.65;
                } else if (kind == 'wave') {
                    var wave = Math.sin(nx * 20 + Math.sin(ny * 7) * 3) + Math.sin(ny * 14 - nx * 11);
                    p = nx * 0.6 + wave * 0.12;
                    value = 0.25 + Math.abs(wave) * 0.38;
                } else if (kind == 'dots') {
                    var gx = Math.floor(nx * 10);
                    var gy = Math.floor(ny * 10);
                    var cx = (gx + 0.5) / 10;
                    var cy = (gy + 0.5) / 10;
                    var d = Math.sqrt(Math.pow(nx - cx, 2) + Math.pow(ny - cy, 2));
                    p = (gx + gy * 2 + effect.id) / 22;
                    value = d < 0.018 + ((gx + gy + effect.id) % 3) * 0.004 ? 1 : 0.02;
                } else {
                    p = nx + ny * 0.28;
                    value = 0.8;
                }

                if (kind == 'sparkle') {
                    value = (rand() > 0.965) ? (0.65 + rand() * 0.35) : 0.02;
                    p = rand();
                }

                var c = palette(p);
                var idx = (y * width + x) * 4;
                image.data[idx] = Math.round(c.r * value);
                image.data[idx + 1] = Math.round(c.g * value);
                image.data[idx + 2] = Math.round(c.b * value);
                image.data[idx + 3] = 255;
            }
        }
        ctx.putImageData(image, 0, 0);
    }

    function drawLines(count, angle, barWidth, alpha) {
        ctx.save();
        ctx.translate(width / 2, height / 2);
        ctx.rotate(angle);
        ctx.translate(-width / 2, -height / 2);
        for (var i = -count; i < count * 2; i++) {
            var c = palette((i + count) / (count * 2));
            ctx.fillStyle = rgb(c, alpha || 0.95);
            ctx.fillRect(i * barWidth * 2, -height, barWidth, height * 3);
        }
        ctx.restore();
    }

    function drawChase(heads, tail) {
        ctx.fillStyle = '#020202';
        ctx.fillRect(0, 0, width, height);
        for (var h = 0; h < heads; h++) {
            var y = height * (h + 0.5) / heads;
            var c = palette(h / Math.max(1, heads - 1));
            var g = ctx.createLinearGradient(18, y, width - 18, y);
            g.addColorStop(0, rgb(c, 0));
            g.addColorStop(0.55, rgb(c, 0.18));
            g.addColorStop(0.82, rgb(c, 0.85));
            g.addColorStop(1, rgb({ r: 255, g: 255, b: 255 }, 1));
            ctx.strokeStyle = g;
            ctx.lineWidth = tail;
            ctx.lineCap = 'round';
            ctx.beginPath();
            ctx.moveTo(18, y + Math.sin(h * 2) * 6);
            ctx.lineTo(width - 18, y - Math.sin(h * 2) * 6);
            ctx.stroke();
        }
    }

    function drawScanner(dual) {
        ctx.fillStyle = '#030303';
        ctx.fillRect(0, 0, width, height);
        var centers = dual ? [0.32, 0.68] : [0.5];
        for (var i = 0; i < centers.length; i++) {
            var x = centers[i] * width;
            var g = ctx.createLinearGradient(x - 42, 0, x + 42, 0);
            g.addColorStop(0, 'rgba(0,0,0,0)');
            g.addColorStop(0.45, rgb(palette(i / 2), 0.35));
            g.addColorStop(0.5, '#ffffff');
            g.addColorStop(0.55, rgb(palette(i / 2 + 0.3), 0.35));
            g.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.fillStyle = g;
            ctx.fillRect(x - 42, 0, 84, height);
        }
    }

    function drawAndroid() {
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, width, height);
        ctx.strokeStyle = rgb(colors[0], 0.22);
        ctx.lineWidth = 10;
        ctx.lineCap = 'round';
        ctx.beginPath();
        ctx.moveTo(22, height * 0.72);
        ctx.lineTo(width - 22, height * 0.28);
        ctx.stroke();
        ctx.strokeStyle = rgb(colors[0], 1);
        ctx.lineWidth = 4;
        ctx.beginPath();
        ctx.moveTo(28, height * 0.70);
        ctx.lineTo(width - 28, height * 0.30);
        ctx.stroke();
    }

    function drawStrobe() {
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, width, height);
        for (var x = -12; x < width + 12; x += 28) {
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(x, 0, 11, height);
            ctx.fillStyle = rgb(colors[1] || colors[0], 0.55);
            ctx.fillRect(x + 13, 0, 5, height);
        }
    }

    ctx.fillStyle = '#020202';
    ctx.fillRect(0, 0, width, height);

    switch (effect.id) {
        case 1:
            ctx.fillStyle = rgb(colors[0]);
            ctx.fillRect(0, 0, width, height);
            vignette();
            break;
        case 2:
            drawAndroid();
            break;
        case 14:
        case 15:
        case 31:
        case 40:
        case 41:
        case 42:
        case 45:
        case 48:
        case 52:
            drawChase(effect.id == 15 ? 3 : (effect.id == 31 ? 6 : 2), effect.id == 48 ? 11 : 7);
            if (effect.id == 41 || effect.id == 42) drawLines(12, -0.78, 4, 0.32);
            break;
        case 18:
        case 29:
        case 30:
        case 34:
            drawScanner(effect.id == 30);
            break;
        case 23:
            drawStrobe();
            break;
        case 25:
            drawPixelField('rainbow');
            break;
        case 26:
        case 27:
        case 28:
            drawPixelField('sparkle');
            break;
        case 35:
        case 53:
            drawPixelField('fire');
            break;
        case 17:
        case 19:
        case 36:
        case 46:
        case 49:
        case 51:
            drawPixelField('noise');
            break;
        case 10:
        case 11:
        case 13:
        case 16:
        case 20:
        case 21:
        case 22:
        case 24:
        case 37:
        case 38:
        case 43:
        case 44:
        case 47:
        case 50:
            drawPixelField('wave');
            drawLines(16, effect.id == 44 ? 0.72 : -0.46, 3 + (effect.id % 3), 0.28);
            break;
        case 12:
        case 33:
        case 39:
            drawPixelField('dots');
            break;
        default:
            drawLines(18, -0.62, 5, 0.95);
            break;
    }

    vignette();
    shade(0.05);
    this.effectGalleryPreviewCache[cacheKey] = canvas.toDataURL('image/png');
    return this.effectGalleryPreviewCache[cacheKey];
}

IgnisLibrary.prototype.syncEffectEditorFromNode = function (node)
{
    if (!node || node.type != 'effect') return;
    this.effectDraft = this.normalizeEffectDraft({
        id: node.effectId,
        speed: node.effectSpeed,
        intensity: node.effectIntensity,
        size: node.effectSize,
        paletteId: node.effectPaletteId,
        colors: node.effectColors
    });
    if ($('#library-effects').is(':visible')) this.renderEffectEditor();
}

IgnisLibrary.prototype.getImageByUid = function (uid)
{
    for (var i in this.library.images) {
        var n = this.library.images[i];

        if (n.uid == uid) return n;
    }
    return false;
}

IgnisLibrary.prototype.load = function (name)
{
    if (!name) name = 'global';

    var libfile = ignis_appdir() + path.sep + name + '.library';

    if (!fs.existsSync(libfile)) return false;

    try {
        var buffer = fs.readFileSync(libfile, 'UTF-8');
        this.library = JSON.parse(buffer);
    } catch (ex) {
        alert('Global library file was corrupted and had to be deleted.')
        fs.unlinkSync(libfile);
        return false;
    }

    return true;
}

IgnisLibrary.prototype.save = function (name)
{
    if (!name) name = 'global';

    var libfile = ignis_appdir() + path.sep + name + '.library';

    fs.writeFileSync(libfile, JSON.stringify(this.library), 'UTF-8');
}

IgnisLibrary.prototype.save = function (name)
{
    if (!name) name = 'global';

    var libfile = ignis_appdir() + path.sep + name + '.library';

    fs.writeFileSync(libfile, JSON.stringify(this.library), 'UTF-8');
}

IgnisLibrary.prototype.genSizes = function (callback)
{
    var tosize = 0;
    var called = false;

    for (var i in this.library.images) {
        var n = this.library.images[i];

        if (n.resolution) continue;

        tosize++;

        var img = new Image();
        img.hash = n.hash;
        img.lib = this;
        img.onload = $.proxy(function (e) {
            var idx = this.getImageIndexByHash(e.target.hash);
            if (idx !== false && this.library.images[idx]) {
                this.library.images[idx].resolution = { w: e.target.width, h: e.target.height, r: e.target.width / e.target.height };
                this.save();
            }
            tosize--;
            if (tosize == 0 && callback && !called) {
                callback();
                called = true;
            }
        }, this);
        img.onerror = $.proxy(function (e) {
            var image = this.getImageByHash(e.target.hash);
            if (image) {
                console.warn('Removing unreadable image from library:', image.path);
                this.removeImage(image.hash);
                this.update();
            }

            tosize--;
            if (tosize == 0 && callback && !called) {
                callback();
                called = true;
            }
        }, this);
        img.src = this.fileUrl(n.path);
    }

    if (tosize == 0 && callback) {
        callback();
    }
}

IgnisLibrary.prototype.getThumbFilePath = function (hash)
{
    return ignis_thumbdir() + path.sep + hash + '.jpg';
}

IgnisLibrary.prototype.getTexFilePath = function (hash)
{
    return ignis_texdir() + path.sep + hash + '.jpg';
}

IgnisLibrary.prototype.genThumbs = function (callback)
{
    var thumbdir = ignis_thumbdir();

    var batch = [];

    for (var i in this.library.images) {
        var n = this.library.images[i];
        var tpath = thumbdir + path.sep + n.hash + '.jpg';

        if (n.thumb && fs.existsSync(tpath)) continue; // we already have thumbnail

        if (fs.existsSync(n.path)) {
            batch.push({ from: n.path, to: tpath });
        }
    }

    if (batch.length > 0) {
        this.ignis.resizer.thumbBatch(batch, config.rendering.thumbnail_quality, function (err, data) {
            app_execute_event('thumbs_generated');
            if (callback) callback(err, data);
        });
    } else {
        app_execute_event('thumbs_generated');
        if (callback) callback(null, null);
    }

    this.genTexs(callback);
}

IgnisLibrary.prototype.genTexs = function (callback)
{
    var texdir = ignis_texdir();
    var texh = (this.ignis.project.leds ? this.ignis.project.leds : config.project.default_leds);

    var batch = [];

    for (var i in this.library.images) {
        var n = this.library.images[i];

        var tpath = texdir + path.sep + n.hash + '_' + texh + '.jpg';

        if (n.tex && fs.existsSync(tpath)) continue; // we already have texture

        if (fs.existsSync(n.path)) {
            batch.push({ from: n.path, to: tpath });
        }
    }

    if (batch.length > 0) {
        this.ignis.resizer.texBatch(batch, texh, config.rendering.texture_quality, function (err, data) {
            app_execute_event('textures_generated');
            if (callback) callback(err, data);
        });
    } else {
        app_execute_event('textures_generated');
        if (callback) callback(err, data);
    }
}

IgnisLibrary.prototype.thumbExist = function (hash)
{
    var image_path = ignis_thumbdir() + path.sep + hash + '.jpg';
    return (fs.existsSync(image_path));
}

IgnisLibrary.prototype.texExist = function (hash)
{
    var image_path = ignis_texdir() + path.sep + hash + '.jpg';
    return (fs.existsSync(image_path));
}

IgnisLibrary.prototype.update = function ()
{
    var thumbdir = ignis_thumbdir();
    var s = ($('#library-content').width() - 5) / 4 - 2;

    for (var i in this.library.images) {
        var n = this.library.images[i];
        if (n.generated) {
            $('#library-images').find('[hash=' + n.hash + ']').remove();
            continue;
        }
        n.type = 'image';

        var tpath = thumbdir + path.sep + n.hash + '.jpg';
        tpath = tpath.split(path.sep).join('/');

        var el = $('#library-images').find('[hash=' + n.hash + ']');
        if (el.length > 0) {
            /*if (el.hasClass('loading') && this.thumbExist(n.hash)) {
                el.removeClass('loading');
                el.addClass('loaded');
                el.css('background-image', 'url(' + tpath + '?t=' + (new Date()).getMilliseconds() + ')');
            }*/
            continue;
        }

        el = $('<div class="library-img"></div>');
        el.attr('hash', n.hash);
        //el.css('background-image', 'url(' + tpath + ')');
        /*if (this.thumbExist(n.hash)) {
            el.addClass('loaded');
            el.css('background-image', 'url(' + tpath + '?t=' + (new Date()).getMilliseconds() + ')');
        } else {
            el.addClass('loading');
            el.css('background-image', 'url(img/img_loading.png)');
        }*/
        
        el.addClass('loading');
        el.css('width', s + 'px');
        el.css('height', s + 'px');
        el.prop('draggable', true);
        el.data('n', n);
        var rotateButton = $('<div class="library-rotate-button" title="Rotate image"><i class="fas fa-redo"></i></div>');
        rotateButton.on('click', $.proxy(function (e) {
            e.preventDefault();
            e.stopPropagation();
            var image = $(e.currentTarget).closest('.library-img').data('n');
            this.rotateLibraryImage(image);
        }, this));
        el.append(rotateButton);
        el.on('dragstart', $.proxy(function (e) {
            this.drag_active = true;
            this.drag_item = $(e.target).data('n');
            e.originalEvent.dataTransfer.effectAllowed = "move";
            e.originalEvent.dataTransfer.setData('itype', 'libimg');
            e.originalEvent.dataTransfer.setData('hash', this.drag_item.hash);
            e.originalEvent.dataTransfer.setData('lib', this.drag_item);
            return true;
        }, this));
        el.on('contextmenu', $.proxy(function (e) {
            $('.delete-popup').remove();
            var del = $('<div class="delete-popup"></div>');
            del.append('<div class="question">Remove from library?</div>');
            var yes = $('<div class="yes">Yes</div>');
            var no = $('<div class="no">No</div>');
            del.append(yes);
            del.append(no);
            del.css('left', e.pageX + 'px');
            del.css('top', e.pageY + 'px');
            yes.attr('hash', $(e.target).attr('hash'));
            $('body').append(del);
            yes.on('click', $.proxy(function (e) {
                $('.delete-popup').remove();
                for (var hash of this.selected_items) {
                    $('.library-img[hash='+hash+']').remove();
                    this.removeImage(hash);
                }
                this.selected_items = [];
            }, this));
            no.on('click', function (e) {
                $('.delete-popup').remove();
            });
        }, this));
        el.on('click', $.proxy(function (e) {
            if ($(e.target).hasClass('selected')) {
                $(e.target).removeClass('selected');
                this.selected_item = null;
                var hash = $(e.target).data('n').hash;
                var idx = this.selected_items.indexOf(hash);
                if (idx >= 0) {
                    this.selected_items.splice(idx, 1);
                }
            } else {
                if (e.ctrlKey) {
                    $(e.target).addClass('selected');
                    this.selected_item = $(e.target).data('n');
                    var hash = $(e.target).data('n').hash;
                    if (!this.selected_items.includes(hash)) {
                        this.selected_items.push(hash);
                    }
                    this.ignis.timeline.deselect();
                    this.ignis.properties.deselect();
                } else {
                    $('#library-images').find('[hash]').removeClass('selected');
                    $(e.target).addClass('selected');
                    this.selected_item = $(e.target).data('n');
                    var hash = $(e.target).data('n').hash;
                    this.selected_items = [hash];
                    this.ignis.timeline.deselect();
                    this.ignis.properties.deselect();
                }
            }
        }, this));
        $('#library-images').append(el);
    }

    $('#library-audio-list').empty();
    for (var i in this.library.audio) {
        var n = this.library.audio[i];
        n.type = 'audio';

        var name = n.name.substring(0, n.name.length - n.ext.length - 1);
        var size = Math.round((n.size / 1024 / 1024) * 100) / 100;
        var duration_m = Math.floor(n.duration / 60);
        var duration_s = Math.round(n.duration - duration_m * 60);
        var duration = duration_m + ':' + (duration_s < 10 ? '0' + duration_s : duration_s);

        var el = $('<tr class="audio-libitem"></tr>');
        el.append($('<td></td>').text(name));
        el.append($('<td></td>').text(n.ext));
        el.append($('<td align="right"></td>').text(duration));
        el.prop('draggable', true);
        el.data('n', n);
        el.attr('hash', n.hash);
        el.on('dragstart', $.proxy(function (e) {
            this.drag_active = true;
            this.drag_item = $(e.target).data('n');
            e.originalEvent.dataTransfer.effectAllowed = "move";
            e.originalEvent.dataTransfer.setData('itype', 'libaudio');
            e.originalEvent.dataTransfer.setData('hash', this.drag_item.hash);
            e.originalEvent.dataTransfer.setData('lib', this.drag_item);
            return true;
        }, this));
        el.on('contextmenu', $.proxy(function (e) {
            $('.delete-popup').remove();
            var del = $('<div class="delete-popup"></div>');
            del.append('<div class="question">Remove from library?</div>');
            var yes = $('<div class="yes">Yes</div>');
            var no = $('<div class="no">No</div>');
            del.append(yes);
            del.append(no);
            del.css('left', e.pageX + 'px');
            del.css('top', e.pageY + 'px');
            yes.attr('hash', $(e.delegateTarget).attr('hash'));
            $('body').append(del);
            yes.on('click', $.proxy(function (e) {
                if (this.selected_audio.length == 0) {
                    this.selected_audio = [$(e.target).attr('hash')];
                }
                $('.delete-popup').remove();
                for (var hash of this.selected_audio) {
                    $('.audio-libitem[hash=' + hash + ']').remove();
                    this.removeAudio(hash);
                }
            }, this));
            no.on('click', function (e) {
                $('.delete-popup').remove();
            });
        }, this));
        el.on('click', $.proxy(function (e) {
            var el = $(e.delegateTarget);
            var hash = el.attr('hash');
            if (!el.hasClass('selected')) {
                if (!e.ctrlKey) {
                    $('.audio-libitem').removeClass('selected');
                    this.selected_audio = [];
                }
                el.addClass('selected');
                if (!this.selected_audio.includes(hash)) {
                    this.selected_audio.push(hash);
                }
            } else {
                el.removeClass('selected');
                var idx = this.selected_audio.indexOf(hash);
                if (idx >= 0) {
                    this.selected_audio.splice(idx, 1);
                }
            }
        }, this));

        $('#library-audio-list').append(el);
    }

    this.renderEffectEditor();

    this.hideGeneratedImages();
}

IgnisLibrary.prototype.renderEffectEditor = function ()
{
    var box = $('#library-effects');
    box.empty();

    for (var i in this.effects) {
        var effect = this.effects[i];
        var draft = this.getDefaultEffectDraft(effect);
        var preview = $('<div class="effect-preview-card library-effect"></div>');
        preview.prop('draggable', true);
        preview.data('n', draft);
        preview.attr('hash', draft.hash);
        var previewImage = this.getStaticEffectPreviewDataUrl(effect, draft);
        preview.append($('<div class="effect-preview-strip"></div>').css('background-image', 'url(' + previewImage + ')'));
        preview.append($('<strong></strong>').text(effect.name));
        preview.on('click', $.proxy(function (e) {
            $('.library-effect').removeClass('selected');
            $(e.currentTarget).addClass('selected');
            this.drag_item = $(e.currentTarget).data('n');
            this.selected_item = this.drag_item;
            this.selected_items = [];
            this.selected_audio = [];
            this.ignis.timeline.deselect();
            this.ignis.properties.deselect();
        }, this));
        preview.on('dragstart', $.proxy(function (e) {
            this.drag_active = true;
            this.drag_item = $(e.currentTarget).data('n');
            e.originalEvent.dataTransfer.effectAllowed = 'move';
            e.originalEvent.dataTransfer.setData('itype', 'libeffect');
            e.originalEvent.dataTransfer.setData('hash', this.drag_item.hash);
            e.originalEvent.dataTransfer.setData('lib', this.drag_item);
            return true;
        }, this));
        box.append(preview);
    }
    this.resize();
}

IgnisLibrary.prototype.getStaticEffectPreviewDataUrl = function (effect, draft)
{
    effect = this.getEffectById(effect && effect.id);
    var cacheKey = 'effect-gallery-fixed-30-' + effect.id;
    if (this.effectGalleryPreviewCache[cacheKey]) return this.effectGalleryPreviewCache[cacheKey];

    var previewImage = this.effectGalleryPreviewDataUrl(effect);
    if (this.ignis.project && this.ignis.project.effectPreviewDataUrl) {
        previewImage = this.ignis.project.effectPreviewDataUrl(draft || this.getDefaultEffectDraft(effect), 100, 100, {
            leds: 30,
            previewScale: 4
        });
    }
    this.effectGalleryPreviewCache[cacheKey] = previewImage;
    return previewImage;
}

IgnisLibrary.prototype.createEffectRange = function (label, key, value, min, max)
{
    var row = $('<label class="effect-range"></label>');
    row.append($('<span></span>').text(label));
    var input = $('<input type="range">').attr({ min: min, max: max, step: 1, value: value, 'data-key': key });
    var out = $('<b></b>').text(value);
    input.on('input', $.proxy(function (e) {
        var key = $(e.currentTarget).attr('data-key');
        var val = parseInt($(e.currentTarget).val());
        var patch = {};
        patch[key] = val;
        $(e.currentTarget).siblings('b').text(val);
        this.updateEffectDraft(patch, true, true);
    }, this));
    input.on('change mouseup touchend', $.proxy(function () {
        this.flushEffectPreviewUpdate();
        this.flushEffectTimelineRefresh();
    }, this));
    row.append(input);
    row.append(out);
    return row;
}

IgnisLibrary.prototype.setEffectColor = function (index, hex)
{
    var next = this.getEffectDraft();
    var slots = this.getEffectColorSlots(next.id);
    if (index < 0 || index >= slots) return;
    next.colors[index] = this.normalizeEffectColor(hex);
    next.paletteId = 0;
    this.updateEffectDraft({ colors: next.colors, paletteId: 0 }, true, true);
    this.refreshEffectColorControls();
}

IgnisLibrary.prototype.refreshEffectColorControls = function ()
{
    var draft = this.getEffectDraft();
    var slots = this.getEffectColorSlots(draft.id);
    if (this.effectColorIndex >= slots) this.effectColorIndex = 0;
    this.drawEffectColorWheel();

    $('#library-effects .effect-color-slot').each($.proxy(function (i, el) {
        var idx = parseInt($(el).attr('data-index'));
        $(el).toggleClass('active', idx == this.effectColorIndex);
        $(el).find('i').css('background-color', draft.colors[idx] || '#000000');
    }, this));

    var color = draft.colors[this.effectColorIndex] || '#000000';
    var hsv = this.effectRgbToHsv(this.effectHexToRgb(color));
    var wheel = $('#library-effects canvas.effect-color-wheel');
    var cursor = $('#library-effects .effect-color-wheel-cursor');
    if (wheel.length && cursor.length) {
        var rect = wheel[0].getBoundingClientRect();
        var radius = Math.min(rect.width, rect.height) / 2;
        var x = rect.width / 2 + Math.cos(hsv.h * Math.PI / 180) * hsv.s * radius;
        var y = rect.height / 2 + Math.sin(hsv.h * Math.PI / 180) * hsv.s * radius;
        cursor.css({ left: x + 'px', top: y + 'px', background: color });
    }
    $('#effect-color-value').val(Math.round(hsv.v * 100));
    $('#effect-color-value-out').text(Math.round(hsv.v * 100) + '%');
}

IgnisLibrary.prototype.drawEffectColorWheel = function ()
{
    var canvas = $('#library-effects canvas.effect-color-wheel')[0];
    if (!canvas || canvas._ignisColorWheelDrawn) return;

    var ctx = canvas.getContext('2d');
    var width = canvas.width;
    var height = canvas.height;
    var cx = width / 2;
    var cy = height / 2;
    var radius = Math.min(cx, cy) - 1;
    var image = ctx.createImageData(width, height);

    for (var y = 0; y < height; y++) {
        for (var x = 0; x < width; x++) {
            var dx = x - cx;
            var dy = y - cy;
            var dist = Math.sqrt(dx * dx + dy * dy);
            var idx = (y * width + x) * 4;
            if (dist > radius) {
                image.data[idx + 3] = 0;
                continue;
            }

            var hue = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360;
            var sat = Math.min(1, dist / radius);
            var rgb = this.effectHexToRgb(this.effectHsvToHex({ h: hue, s: sat, v: 1 }));
            image.data[idx] = rgb.r;
            image.data[idx + 1] = rgb.g;
            image.data[idx + 2] = rgb.b;
            image.data[idx + 3] = 255;
        }
    }

    ctx.putImageData(image, 0, 0);
    canvas._ignisColorWheelDrawn = true;
}

IgnisLibrary.prototype.getEffectColorWheelPoint = function (e, rect)
{
    var original = e.originalEvent || e;
    if (original.touches && original.touches.length) {
        original = original.touches[0];
    } else if (original.changedTouches && original.changedTouches.length) {
        original = original.changedTouches[0];
    }
    return {
        x: original.clientX - rect.left,
        y: original.clientY - rect.top
    };
}

IgnisLibrary.prototype.onEffectColorWheelDown = function (e)
{
    e.preventDefault();
    e.stopPropagation();
    var move = $.proxy(this.onEffectColorWheelMove, this);
    var up = $.proxy(function () {
        $(document).off('mousemove.effectColor touchmove.effectColor', move);
        $(document).off('mouseup.effectColor touchend.effectColor touchcancel.effectColor', up);
        this.flushEffectPreviewUpdate();
        this.flushEffectTimelineRefresh();
    }, this);
    $(document).on('mousemove.effectColor touchmove.effectColor', move);
    $(document).on('mouseup.effectColor touchend.effectColor touchcancel.effectColor', up);
    this.onEffectColorWheelMove(e);
}

IgnisLibrary.prototype.onEffectColorWheelMove = function (e)
{
    if (e && e.preventDefault) e.preventDefault();
    var wheel = $('#library-effects canvas.effect-color-wheel');
    if (!wheel.length) return;
    var rect = wheel[0].getBoundingClientRect();
    var point = this.getEffectColorWheelPoint(e, rect);
    var x = point.x;
    var y = point.y;
    var cx = rect.width / 2;
    var cy = rect.height / 2;
    var dx = x - cx;
    var dy = y - cy;
    var radius = Math.max(1, Math.min(cx, cy));
    var sat = Math.min(1, Math.sqrt(dx * dx + dy * dy) / radius);
    var hue = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360;
    var color = this.getEffectDraft().colors[this.effectColorIndex] || '#000000';
    var hsv = this.effectRgbToHsv(this.effectHexToRgb(color));
    hsv.h = hue;
    hsv.s = sat;
    this.setEffectColor(this.effectColorIndex, this.effectHsvToHex(hsv));
}

IgnisLibrary.prototype.scheduleEffectPreviewUpdate = function ()
{
    if (this.effectPreviewTimer) clearTimeout(this.effectPreviewTimer);
    this.effectPreviewTimer = setTimeout($.proxy(function () {
        this.effectPreviewTimer = null;
        this.updateEffectPreviewCard();
    }, this), 60);
}

IgnisLibrary.prototype.flushEffectPreviewUpdate = function ()
{
    if (this.effectPreviewTimer) {
        clearTimeout(this.effectPreviewTimer);
        this.effectPreviewTimer = null;
    }
    this.updateEffectPreviewCard();
}

IgnisLibrary.prototype.scheduleEffectTimelineRefresh = function ()
{
    if (this.effectTimelineTimer) clearTimeout(this.effectTimelineTimer);
    this.effectTimelineTimer = setTimeout($.proxy(function () {
        this.effectTimelineTimer = null;
        this.refreshEffectTimeline();
    }, this), 80);
}

IgnisLibrary.prototype.flushEffectTimelineRefresh = function ()
{
    if (this.effectTimelineTimer) {
        clearTimeout(this.effectTimelineTimer);
        this.effectTimelineTimer = null;
    }
    this.refreshEffectTimeline();
}

IgnisLibrary.prototype.refreshEffectTimeline = function ()
{
    var timeline = this.ignis.timeline;
    if (!timeline) return;
    timeline.prev_nodes = [];
    timeline.prev_hashes = [];
    if (this.ignis.preview) this.ignis.preview.nodeChanged();
    timeline.updateInactiveTimelines();
    timeline.onProjectUpdated();
}

IgnisLibrary.prototype.updateEffectDraft = function (patch, applyToNode, skipRender)
{
    var next = $.extend(true, {}, this.getEffectDraft(), patch || {});
    this.effectDraft = this.normalizeEffectDraft(next);
    this.drag_item = this.getEffectDraft();
    this.selected_item = this.drag_item;
    this.selected_items = [];
    this.selected_audio = [];

    if (applyToNode) this.applyEffectDraftToSelectedNode();
    if (!skipRender) this.renderEffectEditor();
    else {
        this.refreshEffectColorControls();
        this.scheduleEffectPreviewUpdate();
    }
}

IgnisLibrary.prototype.applyEffectDraftToSelectedNode = function ()
{
    var timeline = this.ignis.timeline;
    var project = this.ignis.project;
    if (!timeline || !project || timeline.editor_multi) return;
    var idx = this.ignis.properties.getActiveTimelineIndex();
    if (idx === null || idx === undefined || !project.timeline[idx]) return;
    var node = project.timeline[idx];
    if (node.type != 'effect') return;

    var draft = this.getEffectDraft();
    node.hash = draft.hash;
    node.effectId = draft.id;
    node.effectName = draft.name;
    node.effectSpeed = draft.speed;
    node.effectIntensity = draft.intensity;
    node.effectSize = draft.size;
    node.effectPaletteId = draft.paletteId;
    node.effectColors = draft.colors.slice(0);

    this.scheduleEffectTimelineRefresh();
}

IgnisLibrary.prototype.updateEffectPreviewCard = function ()
{
    var draft = this.getEffectDraft();
    var effect = this.getEffectById(draft.id);
    var el = $('#library-effects .effect-preview-card.selected');
    if (!el.length) el = $('#library-effects .effect-preview-card[hash="' + draft.hash + '"]');
    if (!el.length) return;
    el.data('n', draft);
    el.attr('hash', draft.hash);
    el.find('strong').text(effect.name);
    el.find('.effect-preview-strip').css('background-image', 'url(' + this.getStaticEffectPreviewDataUrl(effect, draft) + ')');
    this.refreshEffectColorControls();
}

IgnisLibrary.prototype.deselect = function ()
{
    $('.audio-libitem').removeClass('selected');
    $('.library-img').removeClass('selected');
    $('.library-effect').removeClass('selected');
    this.selected_item = null;
    this.selected_items = [];
}

IgnisLibrary.prototype.removeSelectedImages = function ()
{
    $('.delete-popup').remove();
    for (var hash of this.selected_items) {
        $('.library-img[hash='+hash+']').remove();
        this.removeImage(hash);
    }
    this.selected_items = [];
}

IgnisLibrary.prototype.removeAllImages = function ()
{
    if (this.library.images.length == 0) return;
    if (!confirm('Are you sure you want to delete all pictures from the library?')) return;

    var hashes = {};
    var images = this.library.images.slice();

    for (var i in images) {
        hashes[images[i].hash] = true;
        this.removeImage(images[i].hash);
    }

    for (var thash in this.ignis.project.timelines) {
        var tl = this.ignis.project.timelines[thash];
        tl.data = tl.data.filter(function (node) {
            return node && !hashes[node.hash];
        });
    }

    if (this.ignis.project.timelines[this.ignis.project.currentTimeline]) {
        this.ignis.project.timeline = this.ignis.project.timelines[this.ignis.project.currentTimeline].data;
    }

    this.selected_item = null;
    this.selected_items = [];
    $('#library-images').empty();
    this.update();
    this.ignis.timeline.deselect();
    this.ignis.timeline.update();
    this.ignis.preview.nodeChanged();
    app_execute_event('project_updated');
}

IgnisLibrary.prototype.removeImage = function (hash, dontDelete)
{
    var thumbdir = ignis_thumbdir();
    var texdir = ignis_texdir();
    for (var i in this.library.images) {
        var n = this.library.images[i];
        if (n.hash == hash) {
            this.library.images.splice(i, 1);
            this.save();
            var thumb_path = thumbdir + path.sep + n.hash + '.jpg';
            var tex_path = texdir + path.sep + n.hash + '.jpg';
            if (fs.existsSync(thumb_path)) fs.unlinkSync(thumb_path);
            if (fs.existsSync(tex_path)) fs.unlinkSync(tex_path);
            if (!dontDelete && fs.existsSync(n.path)) fs.unlinkSync(n.path);
        }
    }
}

IgnisLibrary.prototype.rotateLibraryImage = function (image)
{
    if (!image) return;

    app_loading(true);
    this.transformImageInPlace(image, { rotate: 90 }, $.proxy(function (item) {
        app_loading(false);
        if (!item) return;

        this.clearSelected();
        this.selected_item = item;
        this.selected_items = [item.hash];
        $('.library-img[hash=' + item.hash + ']').addClass('selected');
    }, this));
}

IgnisLibrary.prototype.removeTexFiles = function (hash)
{
    var texdir = ignis_texdir();
    if (!fs.existsSync(texdir)) return;

    var files = fs.readdirSync(texdir);
    for (var i in files) {
        if (files[i].indexOf(hash + '_') === 0) {
            var filePath = texdir + path.sep + files[i];
            if (fs.existsSync(filePath)) fs.unlinkSync(filePath);
        }
    }
}

IgnisLibrary.prototype.transformImageInPlace = function (source, transform, callback)
{
    if (!source || !source.path) return;

    var img = new Image();
    img.onload = $.proxy(function () {
        var canvas = this.renderTransformedImage(img, transform);
        var data = canvas.toDataURL('image/png').replace(/^data:image\/png;base64,/, '');
        fs.writeFileSync(source.path, Buffer.from(data, 'base64'));

        var thumb_path = this.getThumbFilePath(source.hash);
        var thumb_data = canvas.toDataURL('image/jpeg', 0.92).replace(/^data:image\/jpeg;base64,/, '');
        fs.writeFileSync(thumb_path, Buffer.from(thumb_data, 'base64'));
        ignis_cache.has_thumb[source.hash] = true;

        source.resolution = { w: canvas.width, h: canvas.height, r: canvas.width / canvas.height };
        source.thumb = true;
        source.tex = false;
        this.removeTexFiles(source.hash);
        this.save();
        this.genTexs();
        this.update();

        var thumbUrl = thumb_path.split(path.sep).join('/') + '?t=' + Date.now();
        $('.library-img[hash=' + source.hash + ']')
            .removeClass('loading')
            .css('background-image', 'url(' + thumbUrl + ')');
        $('.timeline-img').each($.proxy(function (idx, element) {
            var node = this.ignis.project.timeline[$(element).attr('idx')];
            if (node && node.hash == source.hash) {
                $(element).css('background-image', 'url(' + thumbUrl + ')');
                node.tex_loaded = false;
            }
        }, this));
        for (var timelineHash in this.ignis.project.timelines) {
            var timeline = this.ignis.project.timelines[timelineHash].data;
            for (var i in timeline) {
                if (timeline[i] && timeline[i].hash == source.hash) {
                    $('.img-inactive[hash=' + timeline[i].uid + ']').css('background-image', 'url(' + thumbUrl + ')');
                }
            }
        }
        this.ignis.timeline.updateInactiveTimelines();
        this.ignis.preview.nodeChanged();

        if (callback) callback(source);
    }, this);

    img.onerror = function () {
        if (callback) callback(null);
    };
    img.src = this.imageFileDataUrl(source.path);
}

IgnisLibrary.prototype.imageFileDataUrl = function (filePath)
{
    var ext = path.extname(filePath).toLowerCase();
    var mime = 'image/png';
    if (ext == '.jpg' || ext == '.jpeg') mime = 'image/jpeg';
    if (ext == '.gif') mime = 'image/gif';
    if (ext == '.bmp') mime = 'image/bmp';

    var data = fs.readFileSync(filePath).toString('base64');
    return 'data:' + mime + ';base64,' + data;
}

IgnisLibrary.prototype.renderTransformedImage = function (img, transform)
{
    var rotate = (transform.rotate || 0) % 360;
    var flipH = !!transform.flipH;
    var flipV = !!transform.flipV;
    var hue = parseInt(transform.hue || 0);
    if (isNaN(hue)) hue = 0;

    var canvas = document.createElement('canvas');
    var ctx = canvas.getContext('2d');
    var swap = Math.abs(rotate) === 90 || Math.abs(rotate) === 270;
    canvas.width = swap ? img.height : img.width;
    canvas.height = swap ? img.width : img.height;

    ctx.save();
    ctx.translate(canvas.width / 2, canvas.height / 2);
    if (rotate) ctx.rotate(rotate * Math.PI / 180);
    ctx.scale(flipH ? -1 : 1, flipV ? -1 : 1);
    if (hue) ctx.filter = 'hue-rotate(' + hue + 'deg)';
    ctx.drawImage(img, -img.width / 2, -img.height / 2);
    ctx.restore();

    return canvas;
}

IgnisLibrary.prototype.transformImage = function (source, transform, callback)
{
    if (!source || !source.path) return;

    var img = new Image();
    img.onload = $.proxy(function () {
        var canvas = this.renderTransformedImage(img, transform);

        var data = canvas.toDataURL('image/png').replace(/^data:image\/png;base64,/, '');
        var tmpdir = ignis_dir('tmp');
        var target = tmpdir + path.sep + source.hash + '_edit_' + Date.now() + '.png';
        fs.writeFileSync(target, Buffer.from(data, 'base64'));

        var hash = this.addFile(target);
        if (fs.existsSync(target)) fs.unlinkSync(target);

        var item = this.getImageByHash(hash);
        var thumb_path = null;
        if (item) {
            item.generated = !!transform.generated;
            thumb_path = this.getThumbFilePath(hash);
            var thumb_data = canvas.toDataURL('image/jpeg', 0.92).replace(/^data:image\/jpeg;base64,/, '');
            fs.writeFileSync(thumb_path, Buffer.from(thumb_data, 'base64'));
            ignis_cache.has_thumb[hash] = true;
            item.resolution = { w: canvas.width, h: canvas.height, r: canvas.width / canvas.height };
            item.thumb = true;
            item.tex = false;
            this.save();
        }

        this.genTexs();
        this.update();
        if (item && item.generated) {
            $('.library-img[hash=' + hash + ']').remove();
        } else if (thumb_path) {
            $('.library-img[hash=' + hash + ']')
                .removeClass('loading')
                .css('background-image', 'url(' + thumb_path.split(path.sep).join('/') + '?t=' + Date.now() + ')');
        }
        if (callback) callback(item);
    }, this);

    img.onerror = function () {
        if (callback) callback(null);
    };
    img.src = source.path;
}

IgnisLibrary.prototype.removeAudio = function (hash, dontDelete)
{
    for (var i in this.library.audio) {
        var n = this.library.audio[i];
        if (n.hash == hash) {
            this.library.audio.splice(i, 1);
            this.save();
            if (!dontDelete && fs.existsSync(n.path)) fs.unlinkSync(n.path);
        }
    }
}

IgnisLibrary.prototype.clearSelected = function ()
{
    $('#library-images').find('[hash]').removeClass('selected');
    $('.library-effect').removeClass('selected');
    this.selected_item = null;
}

IgnisLibrary.prototype.switch = function (what)
{
    $('#library-images').hide();
    $('#library-effects').hide();
    $('#library-audio').hide();
    $('[action^=library_]').removeClass('active');
    
    $('[action=library_' + what + ']').addClass('active');
    $('#library-' + what).show();

    if (what == 'effects') {
        var idx = (this.ignis.properties && this.ignis.properties.getActiveTimelineIndex) ? this.ignis.properties.getActiveTimelineIndex() : null;
        var node = (idx !== null && idx !== undefined && this.ignis.project.timeline[idx]) ? this.ignis.project.timeline[idx] : null;
        if (node && node.type == 'effect') this.syncEffectEditorFromNode(node);
        else this.renderEffectEditor();
    }
}

IgnisLibrary.prototype.resize = function ()
{
    var s = ($('#library-content').width() - 5) / 4 - 2;
    
    $('#library-images').find('.library-img').css('width', s + 'px').css('height', s + 'px');
    $('#library-effects').find('.library-effect').css('width', s + 'px').css('height', s + 'px').css('min-height', s + 'px');
}

IgnisLibrary.prototype.drag = function ()
{
    $('body').on('dragover', $.proxy(function (e) {
        if (this.drag_active) return false;
        $('#drop-overlay').show();
        return false;
    }, this));

    $('body').on('dragend', $.proxy(function (e) {
        this.drag_active = false;
    }, this));

    $('body').on('drop', $.proxy(function (e) {
        this.drag_active = false;
    }, this));

    $('#drop-overlay').on('dragleave', $.proxy(function (e) {
        $('#drop-overlay').hide();
        return false;
    }, this));

    $('#drop-overlay').on('dragend', $.proxy(function (e) {
        $('#drop-overlay').hide();
        return false;
    }, this));

    $('#drop-overlay').on('drop', $.proxy(function (e) {
        $('#drop-overlay').hide();
        e.preventDefault();

        var filePaths = [];
        for (let f of e.originalEvent.dataTransfer.files) {
            filePaths.push(f.path);
        }

        this.importFileList(filePaths);

        return false;
    }, this));
}

IgnisLibrary.prototype.addFile = function (filepath, forcePath)
{
    var ext = path.extname(filepath).toLowerCase().substring(1);
    var base = path.basename(filepath);
    var fnbase = base.substring(0, base.length - ext.length - 1);
    var c = 2;
    var lib_dir = (ext == 'mp3' || ext == 'ogg' ? this.lib_audio_dir : this.lib_image_dir );

    var hash = this.md5file(filepath);
    var target = lib_dir + path.sep + hash + '.' + ext;
    var meta = lib_dir + path.sep + hash + '.meta';

    if (ext == 'jpg' || ext == 'jpeg' || ext == 'png' || ext == 'bmp' || ext == 'gif') {
        // image
        if (this.imageHashExists(hash)) return hash;

        if (filepath != target)
            fs.copyFileSync(filepath, target);

        var n = {
            path: target,
            name: path.basename(target),
            ext: ext,
            size: fs.statSync(target).size,
            hash: hash,
            thumb: false,
            tex: false,
        };
        this.library.images.push(n);
        this.save();
        this.genSizes();

        this.update();
        return n.hash;
    }

    if (ext == 'mp3' || ext == 'ogg') {
        // audio
        if (this.audioHashExists(hash)) return hash;

        if (filepath != target)
            fs.copyFileSync(filepath, target);

        var baseName = path.basename(filepath);
        if (!fs.existsSync(meta)) {
            fs.writeFileSync(meta, path.basename(filepath));
        } else {
            baseName = fs.readFileSync(meta, "utf8");
        }

        var n = {
            path: target,
            name: baseName,
            ext: ext,
            size: fs.statSync(target).size,
            hash: hash,
            duration: -1
        }
        this.library.audio.push(n);
        this.save();

        this.fetchAudioInfo();
        this.update();
        return n.hash;
    }
}

IgnisLibrary.prototype.fetchAudioInfo = function ()
{
    for (var i in this.library.audio) {
        var n = this.library.audio[i];
        if (n.duration > 0) continue;

        var audio = new Audio();
        audio.library = this;
        audio.hash = n.hash;
        audio.oncanplaythrough = function () {
            for (var l in this.library.library.audio) {
                if (this.library.library.audio[l].hash == this.hash) {
                    this.library.library.audio[l].duration = this.duration;
                    this.library.save();
                    this.library.update();
                    break;
                }
            }
        }
        audio.src = n.path;
    }
}
