function IgnisProject(ignis)
{
    this.ignis = ignis;
    ignis.project = this;

    this.name = null;
    this.audio = null;
    this.audio_offset = 10000;
    this.audio_hash = null;
    this.leds = null;
    this.lastLeds = null;
    this.timeline = [];
    this.uniqid = 1;
    this.proc = {};
    this.history = [];
    this.present = null;
    this.future = [];
    this.post = config.project.post_default;
    this.enable_accelerometer = false;
    this.selected_device = '';
    this.lastSignature = '';
    this.historyEnabled = true;
    this.currentTimeline = '';
    this.timelineNameCounter = 1;

    this.timelines = {};

    this.updatedTimeout = null;

    this.path = false;

    this.dialog_open_properties = {
        title: 'Open Ignis Project',
        properties: ['openFile'],
        filters: [
            { name: 'Ignis Projects', extensions: ['ipr'] },
            { name: 'All Files', extensions: ['*'] }
        ],
    };
    this.dialog_save_properties = {
        title: 'Save Ignis Project',
        filters: [
            { name: 'Ignis Projects', extensions: ['ipr'] },
            { name: 'All Files', extensions: ['*'] }
        ],
    };
    this.dialog_export_properties = {
        title: 'Export Ignis Project',
        defaultPath: '',
        filters: [
            { name: 'Ignis Program File', extensions: ['pix'] },
            { name: 'AuraX Program File', extensions: ['axp'] },
            { name: 'All Files', extensions: ['*'] }
        ],
    };
}

IgnisProject.prototype.init = function ()
{
    app_register_action('export', $.proxy(this.export, this));
    app_register_action('export_as', $.proxy(this.exportAs, this));

    app_register_action('project_save', $.proxy(this.save, this));
    app_register_action('project_save_as', $.proxy(this.saveAs, this));

    app_register_action('project_load', $.proxy(this.load, this));

    app_register_action('project_new', $.proxy(this.new, this));

    app_register_action('project_undo', $.proxy(this.historyPop, this));
    app_register_action('project_redo', $.proxy(this.futurePop, this));

    this.new();

    app_register_event('project_updated', $.proxy(this.projectUpdated, this));

    setInterval($.proxy(this.historyCheck, this), 1000);

    //console.log(ignis.config);
    //setInterval($.proxy(this.autoSave, this), 10000);

    $(document).on('keypress', $.proxy(this.keyPress, this));
}

IgnisProject.prototype.clampLineFrequency = function (value)
{
    value = parseInt(value);
    var max = config.project.max_line_frequency || 2500;
    if (isNaN(value)) value = 0;
    if (value < 0) value = 0;
    if (value > max) value = max;
    return value;
}

IgnisProject.prototype.getDefaultLineFrequency = function ()
{
    var value = config.project.node_frequency;
    if (this.ignis.userconf) {
        value = this.ignis.userconf.get('last_line_frequency');
    }
    return this.clampLineFrequency(value);
}

IgnisProject.prototype.clampNodeStart = function (node)
{
    if (!node) return;

    node.start = parseInt(node.start);
    node.duration = parseInt(node.duration);
    node.end = parseInt(node.end);

    if (isNaN(node.start)) node.start = 0;
    if (isNaN(node.duration)) node.duration = 0;
    if (isNaN(node.end)) node.end = node.start + node.duration;

    if (node.start < 0) {
        node.start = 0;
        node.end = node.start + node.duration;
    }
}

IgnisProject.prototype.keyPress = function (e)
{
    if (e.ctrlKey) {
        switch (e.keyCode) {
            case 19: // SAVE
                if (e.shiftKey) {
                    this.saveAs();
                } else {
                    this.save();
                }
                break;
            case 12: // LOAD
                this.load();
                break;
            case 14: // NEW
                this.new();
                break;
            case 5: // EXPORT
                if ($('#properties-export-box').is(':hidden')) {
                    this.ignis.properties.switchExport();
                } else {
                    if (e.shiftKey) {
                        this.exportAs();
                    } else {
                        this.export();
                    }
                }
                break;
            case 20: // ADD TIMELINE
                this.addTimeline();
                app_execute_event('project_updated');
                app_execute_event('project_timelines_updated');
                break;                    
            default:
                break;
        }
    }
}

IgnisProject.prototype.autoSave = function ()
{
    //var fn = ignis_appdir() + path.sep + 'autosave_' + Date.now() + '.ipr';
    //var fn = ignis_appdir() + path.sep + 'autosave.ipr';
    //console.log(fn);
    //this.save(fn, true);
}

IgnisProject.prototype.getCurrentTimeline = function ()
{
    return this.timelines[this.currentTimeline];
}

IgnisProject.prototype.getCurrentPreview = function ()
{
    var pid = this.timelines[this.currentTimeline].preview;
    if (pid >= 0) return pid;
    return false;
}

IgnisProject.prototype.getFreePreview = function ()
{
    var previews = [];
    for (var i = 0; i < this.ignis.preview.instances; i++) previews.push(true);

    for (var h in this.timelines) {
        var tl = this.timelines[h];
        if (previews[tl.preview]) previews[tl.preview] = false;
    }

    for (var i = 0; i < this.ignis.preview.instances; i++) {
        if (previews[i]) return i;
    }
    return false;
}

IgnisProject.prototype.addTimeline = function (fhash)
{
    var hash = window.electronApi.md5('timeline_'+this.name+Math.random()+(new Date()).toString());
    if (fhash) hash = fhash;
    var name = 'Timeline ' + this.timelineNameCounter++;
    this.timelines[hash] = {
        name: name,
        leds: this.leds,
        post: this.post,
        preview: this.getFreePreview(),
        selected_device: this.selected_device,
        data: [],
        history: [],
        future: [],
        present: null,
        default_mode: 0,
    };

    this.ignis.preview.autoAlign();

    return hash;
}

IgnisProject.prototype.setCurrentDefaultMode = function (i)
{
    console.log(this.currentTimeline, i);
    this.timelines[this.currentTimeline].default_mode = i;
}

IgnisProject.prototype.setCurrentPreview = function (i)
{
    for (var hash in this.timelines) {
        var tl = this.timelines[hash];
        if (tl.preview == i) tl.preview = false;
    }

    this.timelines[this.currentTimeline].preview = i;

    app_execute_event('project_updated');
    app_execute_event('project_timelines_updated');
}

IgnisProject.prototype.removeTimeline = function (hash)
{
    if (this.timelinesCount() <= 1) return;

    var next = this.getFirstHashExcept(hash);
    this.switchTimeline(next);

    delete(this.timelines[hash]);

    app_execute_event('project_updated');
    app_execute_event('project_timelines_updated');
}

IgnisProject.prototype.getFirstHashExcept = function (exhash)
{
    for (var hash in this.timelines) {
        if (hash != exhash) return hash;
    }
    return false;
}

IgnisProject.prototype.timelinesCount = function ()
{
    var c = 0;
    for (var hash in this.timelines) {
        c++;
    }
    return c;
}

IgnisProject.prototype.swapTimelines = function(a, b)
{
    var keys = Object.keys(this.timelines);
    for (var i in keys) {
        if (keys[i] == a) {
            keys[i] = b;
            continue;
        }
        if (keys[i] == b) {
            keys[i] = a;
            continue;
        }
    }
    var nts = {};
    for (var i in keys) {
        nts[keys[i]] = this.timelines[keys[i]];
    }
    this.timelines = nts;
    this.switchTimeline(a);
}

IgnisProject.prototype.switchTimeline = function(hash)
{
    if (this.currentTimeline) {
        this.timelines[this.currentTimeline].name = this.timelines[this.currentTimeline].name || this.getTimelineDefaultName(this.currentTimeline);
        this.timelines[this.currentTimeline].post = this.post;
        this.timelines[this.currentTimeline].leds = this.leds;
        this.timelines[this.currentTimeline].selected_device = this.selected_device;
        this.timelines[this.currentTimeline].data = this.timeline;
        this.timelines[this.currentTimeline].history = this.history;
        this.timelines[this.currentTimeline].future = this.future;
        this.timelines[this.currentTimeline].present = this.present;
    }
    this.timeline = this.timelines[hash].data;
    this.selected_device = this.timelines[hash].selected_device;
    this.leds = this.timelines[hash].leds;
    this.post = this.timelines[hash].post;
    this.history = this.timelines[hash].history;
    this.future = this.timelines[hash].future;
    this.present = this.timelines[hash].present;
    if (!this.timelines[hash].name) this.timelines[hash].name = this.getTimelineDefaultName(hash);
    this.currentTimeline = hash;
    app_execute_event('project_updated');
}

IgnisProject.prototype.getTimelineDefaultName = function (hash)
{
    var idx = 1;
    for (var i in this.timelines) {
        if (i == hash) break;
        idx++;
    }
    return 'Timeline ' + idx;
}

IgnisProject.prototype.renameTimeline = function (hash, name)
{
    if (!this.timelines[hash]) return;
    name = (name || '').toString().trim();
    if (name.length == 0) name = this.getTimelineDefaultName(hash);
    this.timelines[hash].name = name;
    app_execute_event('project_updated');
    app_execute_event('project_timelines_updated');
}

IgnisProject.prototype.projectUpdated = function ()
{
    if (this.updatedTimeout !== null) {
        clearTimeout(this.updatedTimeout);
    }

    this.updatedTimeout = setTimeout($.proxy(this.projectUpdatedCommit, this), 1000);
}

IgnisProject.prototype.projectUpdatedCommit = function ()
{
    this.updatedTimeout = null;
    //this.historyPush();
}

IgnisProject.prototype.new = function (dnu, notl)
{
    this.ignis.timeline.pause();
    this.ignis.timeline.cursor_position = 0;

    this.name = config.project.default_name;
    this.audio = null;
    this.audio_hash = null;
    this.audio_offset = 0;
    var savedLeds = this.ignis.userconf ? parseInt(this.ignis.userconf.get('last_leds')) : config.project.default_leds;
    if (isNaN(savedLeds) || savedLeds <= 0) savedLeds = config.project.default_leds;

    this.leds = savedLeds;
    this.post = config.project.post_default;
    this.enable_accelerometer = false;
    this.selected_device = this.ignis.userconf ? (this.ignis.userconf.get('last_selected_device') || '') : '';
    this.timelines = {};
    this.timeline = [];
    this.uniqid = 1;
    this.path = false;

    if (!notl) {
        this.currentTimeline = this.addTimeline();
        // this.addTimeline();
        // this.addTimeline();
        // this.addTimeline();
    }

    this.ignis.audio.clear();
    this.ignis.timeline.update();
    if (!dnu) app_execute_event('project_updated');
    app_execute_event('project_timelines_updated');

    this.ignis.preview.autoAlign();
}

IgnisProject.prototype.getTimelineByPreview = function (i)
{
    for (var hash in this.timelines) {
        var tl = this.timelines[hash];
        if (tl.preview == i) return tl;
    }
    return false;
}

IgnisProject.prototype.load = function (filename)
{
    app_loading(true);
    var library = this.ignis.library;

    if (!filename) {
        filename = dialog_open(this.dialog_open_properties);
    } else {
        filename = [filename];
    }
    if (!filename || filename.length <= 0) {
        app_loading(false);
        return;
    }

    // load file
    var zip_data = fs.readFileSync(filename[0], 'binary');   
    var zip = window.electronApi.readProjectZip(zip_data);

    var project = JSON.parse(zip.projectText);
    var version = parseFloat(project.version);

    if (config.version < version) {
        alert('Can not load project made with more recent version of Ignis Studio!');
        app_loading(false);
        return;
    }

    this.new(true, true);
    this.path = filename[0];
    this.name = project.name;
    //this.leds = project.leds;
    //this.post = (project.post ? project.post : config.project.post_default);
    this.enable_accelerometer = false;
    //this.selected_device = (project.selected_device ? project.selected_device : project.leds+'_C');
    this.uniqid = project.uniqid;

    if (project.audio) {
        this.audio_offset = project.audio_offset;
        var audio = this.ignis.library.getAudioByHash(project.audio_hash);
        if (audio) {
            this.audio = audio.path;
            this.audio_hash = audio.hash;
            this.ignis.audio.loadFile(audio.path);
        } else {
            var dir = ignis_dir('imported');
            var audio_path = dir + path.sep + project.audio;

            if (!fs.existsSync(audio_path)) {
                var audioData = zip.audio(project.audio_hash);
                fs.writeFileSync(audio_path, audioData, 'binary');
                audioData = null;
            }

            this.audio_hash = library.addFile(audio_path);
            fs.unlinkSync(audio_path);
            this.audio = library.getAudioByHash(this.audio_hash).path;
            this.ignis.audio.loadFile(this.audio);
        }
    }

    var images = {};
    var image_translate = {};

    for (var hash in project.images) {
        var image_name = project.images[hash];
        var image = this.ignis.library.getImageByHash(hash);
        if (image) {
            images[hash] = image;
            image_translate[hash] = hash;
        } else {
            ignis_dir('imported');
            var dir = ignis_dir('imported');
            var image_path = dir + path.sep + image_name;

            if (fs.existsSync(image_path)) {
                fs.unlinkSync(image_path);
            }

            var imageData = zip.image(hash);
            fs.writeFileSync(image_path, imageData, 'binary');
            imageData = null;
            var new_hash = this.ignis.library.addFile(image_path);
            fs.unlinkSync(image_path);
            image_translate[hash] = new_hash;
        }
    }

    app_execute_event('project_updated');
    this.ignis.preview.autoAlign();

    this.ignis.library.genSizes($.proxy(function () {
        if (project.timelines) {
            for (var hash in project.timelines) {
                var tl = project.timelines[hash];
                this.timeline = [];
                var nhash = this.addTimeline(hash);
                this.currentTimeline = null;
                this.switchTimeline(nhash);
                this.setLeds(tl.leds);
                this.post = (tl.post !== undefined && tl.post !== null) ? tl.post : this.post;
                this.selected_device = tl.selected_device || (tl.leds + '_C');
                for (var i in tl.data) {
                    var n = tl.data[i];
                    var lib = this.ignis.library.getImageByHash(image_translate[n.hash]);
                    var nn = this.addNode(lib.hash, lib.path, n.start, n.duration);
                    if (n.gap) nn.gap = n.gap;
                    if (n.frequency) nn.frequency = this.clampLineFrequency(n.frequency);
                    if (n.picture_frequency) nn.picture_frequency = n.picture_frequency;
                    if (n.preview_type) nn.preview_type = n.preview_type;
                    if (n.mirror) nn.mirror = n.mirror;
                    if (n.rotate) nn.rotate = n.rotate;
                    if (n.reverse) nn.reverse = n.reverse;
                    if (n.mgap) nn.mgap = n.mgap;
                }
                this.timelines[hash].data = this.timeline;
                this.timelines[hash].leds = tl.leds;
                this.timelines[hash].post = (tl.post !== undefined && tl.post !== null) ? tl.post : this.post;
                this.timelines[hash].selected_device = tl.selected_device || (tl.leds + '_C');
                this.timelines[hash].name = tl.name || this.getTimelineDefaultName(hash);
                if (tl.default_mode !== null && tl.default_mode !== undefined)
                    this.timelines[hash].default_mode = tl.default_mode;
                this.setLeds(tl.leds);
            }
            for (var h in this.timelines) {
                this.currentTimeline = null;
                this.ignis.timeline.switchTimeline(h);
                break;
            }
        }
        
        if (project.timeline) {
            for (var i in project.timeline) {
                var n = project.timeline[i];
                var lib = this.ignis.library.getImageByHash(image_translate[n.hash]);
                var nn = this.addNode(lib.hash, lib.path, n.start, n.duration);
                if (n.gap) nn.gap = n.gap;
                if (n.frequency) nn.frequency = this.clampLineFrequency(n.frequency);
                if (n.picture_frequency) nn.picture_frequency = n.picture_frequency;
                if (n.preview_type) nn.preview_type = n.preview_type;
                if (n.mirror) nn.mirror = n.mirror;
                if (n.rotate) nn.rotate = n.rotate;
                if (n.reverse) nn.reverse = n.reverse;
                if (n.mgap) nn.mgap = n.mgap;
            }
            var hash = this.currentTimeline = this.addTimeline();
            this.timelines[hash].data = this.timeline;
            this.timelines[hash].leds = this.leds;
            this.timelines[hash].post = this.post;
        }

        app_execute_event('project_timelines_updated');
    }, this));

    this.ignis.library.genThumbs($.proxy(function () {
        this.recalculate();
        app_loading(false);
    }, this));
}

IgnisProject.prototype.setLeds = function (leds)
{
    if (leds == this.leds) return;
    if (this.timelines[this.currentTimeline] == undefined) return;
    //console.log("Set LEDs: " + leds);

    this.timelines[this.currentTimeline].leds = this.leds = leds;
    if (!this.selected_device) this.timelines[this.currentTimeline].selected_device = this.selected_device = leds + '_C';

    app_execute_event('project_leds_updated');
}

IgnisProject.prototype.serializeTimelineData = function (data)
{
    var timeline = [];

    for (var i in data) {
        var n = data[i];
        if (n == null || n == undefined) continue;

        timeline.push({
            type: n.type || 'image',
            start: n.start,
            end: n.end,
            duration: n.duration,
            name: n.effectName || path.basename(n.path || ''),
            path: n.path,
            hash: n.hash,
            effectId: n.effectId,
            effectName: n.effectName,
            effectSpeed: n.effectSpeed,
            effectIntensity: n.effectIntensity,
            effectSize: n.effectSize,
            effectPaletteId: n.effectPaletteId,
            effectColors: n.effectColors,
            sep: n.sep,
            dim: n.dim,
            sync: n.sync,
            strobo: n.strobo,
            uid: n.uid,
            gap: n.gap,
            frequency: this.clampLineFrequency(n.frequency),
            picture_frequency: n.picture_frequency,
            accelerometer: false,
            preview_type: n.preview_type,
            mirror: n.mirror,
            rotate: n.rotate,
            reverse: n.reverse,
            effectRotate180: n.effectRotate180,
            effectFlipH: n.effectFlipH,
            effectFlipV: n.effectFlipV,
            mgap: n.mgap
        });
    }

    return timeline;
}

IgnisProject.prototype.generateData = function ()
{
    if (this.currentTimeline && this.timelines[this.currentTimeline]) {
        this.timelines[this.currentTimeline].post = this.post;
        this.timelines[this.currentTimeline].leds = this.leds;
        this.timelines[this.currentTimeline].selected_device = this.selected_device;
        this.timelines[this.currentTimeline].data = this.timeline;
        this.timelines[this.currentTimeline].history = this.history;
        this.timelines[this.currentTimeline].future = this.future;
        this.timelines[this.currentTimeline].present = this.present;
    }

    var image_hashes = {};
    var timelines = {};

    for (var hash in this.timelines) {
        var tl = this.timelines[hash];
        var timeline = this.serializeTimelineData(tl.data);
        for (var i in timeline) {
            if (timeline[i].type != 'effect') image_hashes[timeline[i].hash] = timeline[i].name;
        }

        timelines[hash] = {
            name: tl.name || this.getTimelineDefaultName(hash),
            data: timeline,
            leds: tl.leds,
            post: tl.post,
            preview: tl.preview,
            selected_device: tl.selected_device,
            default_mode: tl.default_mode,
        };
    }

    var currentTimeline = this.currentTimeline;
    var currentData = this.serializeTimelineData(this.timeline);

    var project_data = {
        version: config.version,
        name: this.name,
        audio: (this.audio ? path.basename(this.audio) : null),
        audio_offset: this.audio_offset,
        audio_hash: this.audio_hash,
        leds: this.leds,
        post: this.post,
        enable_accelerometer: this.enable_accelerometer,
        selected_device: this.selected_device,
        uniqid: this.uniqid,
        timeline: currentData,
        timelines: timelines,
        currentTimeline: currentTimeline,
        images: image_hashes,
    };

    return project_data;
}

IgnisProject.prototype.save = function (filename, metaonly)
{
    if (filename && !metaonly) {
        this.path = filename;
    }
    if ( this.path === false && !metaonly ) {
        return this.saveAs();
    }
    
    // force timeline to be updated to timelines
    this.switchTimeline(this.currentTimeline);

    var images = {};
    var image_hashes = {};
    //var timeline = [];
    var timelines = {};

    for (var hash in this.timelines) {
        var tl = this.timelines[hash];
        var timeline = [];
        for (var i in tl.data) {
            var n = tl.data[i];
            
            if (!images[n.hash]) {
                if (n.type != 'effect') {
                    images[n.hash] = n.path;
                    image_hashes[n.hash] = path.basename(n.path);
                }
            }
    
            timeline.push({
                type: n.type || 'image',
                start: n.start,
                end: n.end,
                duration: n.duration,
                name: n.effectName || path.basename(n.path || ''),
                hash: n.hash,
                effectId: n.effectId,
                effectName: n.effectName,
                effectSpeed: n.effectSpeed,
                effectIntensity: n.effectIntensity,
                effectSize: n.effectSize,
                effectPaletteId: n.effectPaletteId,
                effectColors: n.effectColors,
                sep: n.sep,
                dim: n.dim,
                sync: n.sync,
                strobo: n.strobo,
                uid: n.uid,
                gap: n.gap,
                frequency: this.clampLineFrequency(n.frequency),
                picture_frequency: n.picture_frequency,
                accelerometer: false,
                preview_type: n.preview_type,
                mirror: n.mirror,
                rotate: n.rotate,
                reverse: n.reverse,
                effectRotate180: n.effectRotate180,
                effectFlipH: n.effectFlipH,
                effectFlipV: n.effectFlipV,
                mgap: n.mgap
            });
        }
        timelines[hash] = {
            name: tl.name || this.getTimelineDefaultName(hash),
            data: timeline,
            leds: tl.leds,
            post: tl.post,
            preview: tl.preview,
            selected_device: tl.selected_device,
            default_mode: tl.default_mode,
        };
    }

    /*for (var i in this.timeline) {
        var n = this.timeline[i];
        
        if (!images[n.hash]) {
            images[n.hash] = n.path;
            image_hashes[n.hash] = path.basename(n.path);
        }

        timeline.push({
            start: n.start,
            end: n.end,
            duration: n.duration,
            name: path.basename(n.path),
            hash: n.hash,
            sep: n.sep,
            dim: n.dim,
            sync: n.sync,
            strobo: n.strobo,
            uid: n.uid,
            gap: n.gap,
            frequency: this.clampLineFrequency(n.frequency),
            picture_frequency: n.picture_frequency,
            accelerometer: false,
            preview_type: n.preview_type,
            mirror: n.mirror,
            rotate: n.rotate,
            reverse: n.reverse,
            mgap: n.mgap
        });
    }*/

    var project_data = {
        version: config.version,
        name: this.name,
        audio: (this.audio ? path.basename(this.audio) : null),
        audio_offset: this.audio_offset,
        audio_hash: this.audio_hash,
        leds: this.leds,
        post: this.post,
        uniqid: this.uniqid,
        //timeline: timeline,
        timelines: timelines,
        images: image_hashes,
    };

    var zip_images = {};
    var zip_audio = null;
    
    if (!metaonly) {
        for (var hash in images) {
            zip_images[hash] = fs.readFileSync(images[hash], 'binary');
        }

        if (this.audio) {
            zip_audio = {
                hash: this.audio_hash,
                data: fs.readFileSync(this.audio, 'binary')
            };
        }
    }

    var zip_data = window.electronApi.createProjectZip(JSON.stringify(project_data), zip_images, zip_audio);
    fs.writeFileSync(this.path, zip_data, 'binary');
}

IgnisProject.prototype.saveAs = function ()
{
    this.dialog_save_properties.defaultPath = this.name + ".ipr";
    var fn = dialog_save(this.dialog_save_properties);
    if (fn) {
        this.path = fn;
        this.save();
    }
}

IgnisProject.prototype.addNodeIq = function (hash, fpath, start, duration, activate)
{
    if (start < 0) start = 0;
    var end = start + duration;

    for (var i in this.timeline) {
        var n = this.timeline[i];

        if (n.start <= end && n.start > start) {
            end = n.start - 1;
        }
    }

    var orig_duration = duration;
    duration = end - start;
    var short = orig_duration - duration;

    /*if (short > 0) {
        var new_start = start;
        for (var i in this.timeline) {
            var n = this.timeline[i];

            if (n.end < start && start - n.end <= short && n.end + 1 > new_start) {
                new_start = n.end + 1;
            }
        }
        start = new_start;
        duration = end - start;
    }*/

    return this.addNode(hash, fpath, start, duration, activate);
}

IgnisProject.prototype.addNode = function (hash, fpath, start, duration, activate)
{
    if (start < 0) start = 0;

    var ln = this.ignis.library.getImageByHash(hash);
    if (!ln) {
        var fx = this.ignis.library.getEffectByHash(hash);
        if (!fx) return;
        var draft = (this.ignis.library.drag_item && this.ignis.library.drag_item.type == 'effect' && this.ignis.library.drag_item.id == fx.id)
            ? this.ignis.library.drag_item
            : fx;
        var effectNode = {
            type: 'effect',
            start: start,
            end: start + duration,
            duration: duration,
            path: '',
            hash: 'effect:' + fx.id,
            effectId: fx.id,
            effectName: fx.name,
            effectSpeed: draft.speed || 500,
            effectIntensity: (draft.intensity !== undefined && draft.intensity !== null) ? draft.intensity : 128,
            effectSize: draft.size || 3,
            effectPaletteId: (draft.paletteId !== undefined && draft.paletteId !== null) ? draft.paletteId : 0,
            effectColors: (draft.colors || fx.colors || ['#ff6000', '#00b4ff', '#ffffff']).slice(0),
            sep: 80,
            dim: 100,
            sync: false,
            strobo: false,
            uid: this.uniqid++,
            tex_loaded: true,
            gap: 0,
            frequency: 40,
            picture_frequency: 1,
            accelerometer: false,
            preview_type: ( this.timelines[this.currentTimeline] ? this.timelines[this.currentTimeline].default_mode : 0 ),
            mirror: false,
            rotate: false,
            reverse: false,
            effectRotate180: false,
            effectFlipH: false,
            effectFlipV: false,
            mgap: 0
        };
        if (activate) effectNode.activate = activate;
        this.timeline.push(effectNode);
        this.recalculate(this.timeline.length - 1);
        return effectNode;
    }
    var leds_height = parseInt(this.ignis.project.leds);
    var leds_width = Math.round(leds_height * ln.resolution.r);
    
    var freq = this.getDefaultLineFrequency();
    if (config.project.node_calculate_frequency) {
        freq = leds_width * 4;
    }
    freq = this.clampLineFrequency(freq);

    var n = {
        start: start,
        end: start + duration,
        duration: duration,
        path: fpath,
        hash: hash,
        sep: 80,
        dim: 100,
        sync: false,
        strobo: false,
        uid: this.uniqid++,
        tex_loaded: false,
        gap: 0,
        frequency: freq,
        picture_frequency: 1,
        accelerometer: false,
        preview_type: ( this.timelines[this.currentTimeline] ? this.timelines[this.currentTimeline].default_mode : 0 ),
        mirror: false,
        rotate: false,
        reverse: false,
        mgap: 0
    };
    if (activate) n.activate = activate;
    this.timeline.push(n);
    this.recalculate(this.timeline.length - 1);

    return n;
}

//IgnisProject.prototype.updateNode = function (hash)

IgnisProject.prototype.deleteNode = function (i) {
    this.historyPush();
    this.timeline[i] = null;
    this.commitDelete();
    app_execute_event('project_updated');
}

IgnisProject.prototype.futurePop = function ()
{
    if (this.future.length <= 0) return;
    this.historyEnabled = false;

    this.pushHistoryState(this.generateData());
    var targetState = this.future.pop();
    var nextHistory = this.history.slice();
    var nextFuture = this.future.slice();
    this.present = targetState;
    
    this.applyProjectData(this.present);
    this.history = nextHistory;
    this.future = nextFuture;
    this.present = this.generateData();
    this.lastSignature = this.dataSignature(this.present);
    this.limitHistoryStacks();
    this.syncHistoryStacksToCurrentTimeline();

    this.historyEnabled = true;
}

IgnisProject.prototype.historyPop = function ()
{
    if (this.history.length <= 0) return;
    this.historyEnabled = false;

    this.ignis.timeline.clearSelectionVisuals();

    this.pushFutureState(this.generateData());

    var targetState = this.history.pop();
    var nextHistory = this.history.slice();
    var nextFuture = this.future.slice();
    this.present = targetState;
    this.applyProjectData(this.present);
    this.history = nextHistory;
    this.future = nextFuture;
    this.present = this.generateData();
    this.lastSignature = this.dataSignature(this.present);
    this.limitHistoryStacks();
    this.syncHistoryStacksToCurrentTimeline();

    this.historyEnabled = true;
}

IgnisProject.prototype.historyCheck = function ()
{
    if (!this.historyEnabled) return;

    var historyData = this.generateData();
    var signature = this.dataSignature(historyData);

    if (this.present == null) {
        this.present = historyData;
        this.lastSignature = signature;
        return;
    }

    if (this.lastSignature == '') {
        this.lastSignature = signature;
        return;
    }

    if (signature != this.lastSignature) {
        this.lastSignature = signature;
        this.historyPushPresent();
        this.present = historyData;
        this.future = [];
        this.limitHistoryStacks();
    }
}

IgnisProject.prototype.cloneHistoryData = function (data)
{
    if (!data) return null;
    return JSON.parse(JSON.stringify(data));
}

IgnisProject.prototype.pushHistoryState = function (data)
{
    var state = this.cloneHistoryData(data);
    if (!state) return;

    var signature = this.dataSignature(state);
    if (this.history.length > 0) {
        var last = this.history[this.history.length - 1];
        if (this.dataSignature(last) == signature) return;
    }

    this.history.push(state);
    this.limitHistoryStacks();
}

IgnisProject.prototype.pushFutureState = function (data)
{
    var state = this.cloneHistoryData(data);
    if (!state) return;

    var signature = this.dataSignature(state);
    if (this.future.length > 0) {
        var last = this.future[this.future.length - 1];
        if (this.dataSignature(last) == signature) return;
    }

    this.future.push(state);
    this.limitHistoryStacks();
}

IgnisProject.prototype.syncHistoryStacksToCurrentTimeline = function ()
{
    if (!this.currentTimeline || !this.timelines[this.currentTimeline]) return;
    this.timelines[this.currentTimeline].history = this.history;
    this.timelines[this.currentTimeline].future = this.future;
    this.timelines[this.currentTimeline].present = this.present;
}

IgnisProject.prototype.futurePushPresent = function ()
{
    this.pushFutureState(this.present);
}

IgnisProject.prototype.historyPushPresent = function ()
{
    this.pushHistoryState(this.present);
}

IgnisProject.prototype.historyPushEx = function ()
{
    this.historyEnabled = false;
    // we are changing history, so we don't know future yet
    this.future = [];

    // push current state to history
    var historyData = this.generateData();
    this.history.push(historyData);

    // limit history & future stack size
    this.limitHistoryStacks();

    this.historyEnabled = true;
}

IgnisProject.prototype.historyPush = function ()
{
    if (!this.historyEnabled) return;

    // we are changing history, so we don't know future yet
    this.future = [];

    // push current state to history
    var historyData = this.generateData();
    this.present = historyData;
    this.lastSignature = this.dataSignature(historyData);
    this.pushHistoryState(historyData);

    // limit history & future stack size
    this.limitHistoryStacks();
}

IgnisProject.prototype.dataSignature = function (data)
{
    return window.electronApi.md5(JSON.stringify(data));
}

IgnisProject.prototype.limitHistoryStacks = function ()
{
    while (this.history.length > config.project.history_limit) this.history.splice(0, 1);
    while (this.future.length > config.project.history_limit) this.future.splice(0, 1);
}

IgnisProject.prototype.createNodeFromData = function (n)
{
    var lib = this.ignis.library.getImageByHash(n.hash);
    var fx = (!lib && (n.type == 'effect' || String(n.hash || '').indexOf('effect:') == 0)) ? this.ignis.library.getEffectByHash(n.hash) : null;
    if (!lib && !fx && !n.path) return null;

    var start = parseInt(n.start);
    var duration = parseInt(n.duration);
    var end = parseInt(n.end);
    if (isNaN(start)) start = 0;
    if (isNaN(duration)) {
        duration = (!isNaN(end) ? end - start : config.timeline.default_duration);
    }
    if (isNaN(end)) end = start + duration;
    if (duration <= 0) duration = end - start;

    var uid = parseInt(n.uid);
    if (isNaN(uid)) uid = this.uniqid++;
    if (uid >= this.uniqid) this.uniqid = uid + 1;

    var node = {
        type: fx ? 'effect' : (n.type || 'image'),
        start: start,
        end: start + duration,
        duration: duration,
        path: fx ? '' : (n.path || lib.path),
        hash: n.hash,
        effectId: n.effectId || (fx ? fx.id : undefined),
        effectName: n.effectName || (fx ? fx.name : undefined),
        effectSpeed: (n.effectSpeed !== undefined && n.effectSpeed !== null) ? n.effectSpeed : (fx ? 500 : undefined),
        effectIntensity: (n.effectIntensity !== undefined && n.effectIntensity !== null) ? n.effectIntensity : (fx ? 128 : undefined),
        effectSize: (n.effectSize !== undefined && n.effectSize !== null) ? n.effectSize : (fx ? 3 : undefined),
        effectPaletteId: (n.effectPaletteId !== undefined && n.effectPaletteId !== null) ? n.effectPaletteId : (fx ? 0 : undefined),
        effectColors: n.effectColors || (fx ? fx.colors : undefined),
        sep: (n.sep !== undefined && n.sep !== null) ? n.sep : 80,
        dim: (n.dim !== undefined && n.dim !== null) ? n.dim : 100,
        sync: !!n.sync,
        strobo: !!n.strobo,
        uid: uid,
        tex_loaded: false,
        gap: n.gap || 0,
        frequency: this.clampLineFrequency(n.frequency || (fx ? 40 : this.getDefaultLineFrequency())),
        picture_frequency: n.picture_frequency || 1,
        accelerometer: false,
        preview_type: n.preview_type || 0,
        mirror: !!n.mirror,
        rotate: !!n.rotate,
        reverse: !!n.reverse,
        effectRotate180: !!n.effectRotate180,
        effectFlipH: !!n.effectFlipH,
        effectFlipV: !!n.effectFlipV,
        mgap: n.mgap || 0
    };

    this.clampNodeStart(node);
    return node;
}

IgnisProject.prototype.applyTimelineData = function (data)
{
    var timeline = [];
    for (var i in data) {
        var node = this.createNodeFromData(data[i]);
        if (node) timeline.push(node);
    }
    return timeline;
}

IgnisProject.prototype.applyProjectData = function (project)
{
    if (!project) return;

    if (this.ignis.timeline) {
        this.ignis.timeline.resetTimelineDom();
    }

    this.name = project.name || this.name;
    this.audio_offset = (project.audio_offset !== undefined && project.audio_offset !== null) ? project.audio_offset : this.audio_offset;
    this.audio_hash = project.audio_hash || this.audio_hash;
    this.leds = project.leds || this.leds;
    this.post = (project.post !== undefined && project.post !== null) ? project.post : this.post;
    this.enable_accelerometer = false;
    this.selected_device = project.selected_device || this.selected_device;
    if (project.uniqid && project.uniqid > this.uniqid) this.uniqid = project.uniqid;

    if (project.timelines) {
        var oldCurrentTimeline = this.currentTimeline;
        this.timelines = {};

        for (var hash in project.timelines) {
            var tl = project.timelines[hash];
            this.timelines[hash] = {
                name: tl.name || this.getTimelineDefaultName(hash),
                leds: tl.leds || this.leds,
                post: (tl.post !== undefined && tl.post !== null) ? tl.post : this.post,
                preview: (tl.preview !== undefined && tl.preview !== null) ? tl.preview : false,
                selected_device: tl.selected_device || this.selected_device,
                data: this.applyTimelineData(tl.data || []),
                history: [],
                future: [],
                present: null,
                default_mode: tl.default_mode || 0,
            };
        }

        var targetTimeline = project.currentTimeline || oldCurrentTimeline;
        if (!this.timelines[targetTimeline]) {
            for (var firstHash in this.timelines) {
                targetTimeline = firstHash;
                break;
            }
        }

        this.currentTimeline = null;
        this.switchTimeline(targetTimeline);
        app_execute_event('project_timelines_updated');
        app_execute_event('project_updated');
        return;
    }

    this.timeline = this.applyTimelineData(project.timeline || []);
    this.recalculate();
}

IgnisProject.prototype.recalculate = function (protected_index, no_history)
{
    if (protected_index !== null && protected_index !== undefined) {
        this.fixIndex(protected_index);
    }

    this.commitDelete();

    for (var i = 0; i < this.timeline.length; i++) {
        this.fixIndex(i);
    }

    this.commitDelete();

    app_execute_event('project_updated');
}

IgnisProject.prototype.commitDelete = function ()
{
    this.timeline = this.timeline.filter(function (el) {
        return el != null;
    });
}

IgnisProject.prototype.fixIndex = function (index)
{
    var del = [];

    var n = this.timeline[index];

    if (n == null || n == undefined) return;

    this.clampNodeStart(n);

    if (n.start >= n.end || n.duration <= 0) {
        this.timeline[index] = null;
        this.commitDelete();
        return;
    }

    // find conflicts
    for (var i = 0; i < this.timeline.length; i++) {
        if (i == index) continue;

        var t = this.timeline[i];

        if (t == undefined || t == null) continue;

        // join
        if (this.canJoinTimelineNodes(t, n)) {
            var s = (t.start < n.start ? t.start : n.start);
            var e = (t.end > n.end ? t.end : n.end);
            n.start = s;
            n.end = e;
            n.duration = e - s;
            this.timeline[i] = null;
            continue;
        }

        // delete if completely overlaped
        if (t.start >= n.start && t.end <= n.end) {
            this.timeline[i] = null;
            continue;
        }

        // cut in
        if (t.start < n.start && t.end > n.end) {
            var c = {
                type: t.type || 'image',
                start: n.end + 1,
                end: t.end,
                duration: t.end - n.end + 1,
                path: t.path,
                hash: t.hash,
                effectId: t.effectId,
                effectName: t.effectName,
                effectSpeed: t.effectSpeed,
                effectIntensity: t.effectIntensity,
                effectSize: t.effectSize,
                effectPaletteId: t.effectPaletteId,
                effectColors: t.effectColors ? t.effectColors.slice(0) : t.effectColors,
                sep: t.sep,
                dim: t.dim,
                sync: t.sync,
                strobo: t.strobo,
                gap: t.gap,
                frequency: this.clampLineFrequency(t.frequency),
                picture_frequency: t.picture_frequency,
                accelerometer: false,
                preview_type: t.preview_type,
                mirror: t.mirror,
                rotate: t.rotate,
                reverse: t.reverse,
                effectRotate180: t.effectRotate180,
                effectFlipH: t.effectFlipH,
                effectFlipV: t.effectFlipV,
                mgap: t.mgap,
                uid: this.uniqid++
            };
            this.timeline.push(c);
            t.end = n.start - 1;
            t.duration = t.end - t.start;
            continue;
        }

        // left
        if (t.end >= n.start && t.start < n.start) {
            t.end = n.start - 1;
            t.duration = t.end - t.start;
            continue;
        }

        // right
        if (t.start <= n.end && t.end > n.end) {
            t.start = n.end + 1;
            t.duration = t.end - t.start;
            continue;
        }
    }
}

IgnisProject.prototype.exportAs = function ()
{
    this.export( true );
}

IgnisProject.prototype.sortTimeline = function ()
{
    this.timeline.sort(this.sortCompare);
}

IgnisProject.prototype.sortCompare = function (a, b)
{
    if (a.start > b.start) return 1;
    if (b.start > a.start) return -1;
  
    return 0;
}

IgnisProject.prototype.getMirrorSuffix = function (node)
{
    return (node.rotate ? '_r' : '_m') + (node.reverse ? 'v' : '') + node.mgap;
}

IgnisProject.prototype.getMirrorImageHash = function (node, leds)
{
    return node.hash + this.getMirrorSuffix(node) + '_' + leds;
}

IgnisProject.prototype.export = function (savedialog)
{
    if (this.timeline.length == 0) return;

    app_loading(true);
    this.saveas = (savedialog ? true : false);

    this.prepareExport((data) => {
        this.ignis.timeline.deselect();
        this.sortTimeline();
        this.ignis.timeline.reindex();
        if ( config.project.debug_export ) { // DEBUG OUTPUT
            this.debug_export(data);
            app_loading(false);
        } else { // STANDARD EXPORT
            // docasne zmenit hashe pro mirrorovane obrazky
            for (var i in this.timeline) {
                var n = this.timeline[i];
                if (n.type != 'effect' && n.mirror) {
                    this.timeline[i].hash = this.getMirrorImageHash(n, this.leds);
                }
            }

            //console.log(data); return;

            var target = $('#drives').val();

            if (!target || target.length <= 0) {
                for (var i in this.timeline) {
                    var n = this.timeline[i];
                    if (n.type != 'effect' && n.mirror) {
                        this.timeline[i].hash = this.timeline[i].hash.split('_')[0];
                    }
                }
                app_loading(false);
                return;
            }

            if (target[target.length-1] != path.sep) target += path.sep;

            var requestedExt = this.getExportExtension();
            var fn = target + this.getProjectFilename(false, requestedExt);
            if ($('#filename-editable-btn').hasClass('active')) {
                fn = target + this.sanitizeExportFilename($('#export-filename').val(), requestedExt);
            }

            if (this.saveas) {
                var tfn = remove_diacritics($('#project-name').val()).toLowerCase();
                tfn = tfn.replace(/[^a-zA-Z0-9]+/g, ' ').trim().replace(/[ ]+/g, '-');
                tfn = this.getProjectFilename(true, requestedExt);

                this.configureExportDialogFilters(requestedExt);
                this.dialog_export_properties.defaultPath = tfn;
                fn = dialog_save(this.dialog_export_properties);
            }

            if (!fn) {
                for (var i in this.timeline) {
                    var n = this.timeline[i];
                    if (n.type != 'effect' && n.mirror) {
                        this.timeline[i].hash = this.timeline[i].hash.split('_')[0];
                    }
                }
                app_loading(false);
                return;
            }

            var finalTechnology = this.getExportTechnologyForFilename(fn);
            var exportProcessor = finalTechnology == 'aurax' ? processor_process_aurax : processor_process;

            exportProcessor(this, data, $.proxy(function (result) {

                // vratit hashe zpatky
                for (var i in this.timeline) {
                    var n = this.timeline[i];
                    if (n.type != 'effect' && n.mirror) {
                        this.timeline[i].hash = this.timeline[i].hash.split('_')[0];
                    }
                }

                if (fs.existsSync(fn)) {
                    fs.unlinkSync(fn);
                }

                fs.writeFile(fn, result, $.proxy(function (e) {
                    app_loading(false);
                    if (e === null) {
                        this.ignis.properties.updateDeviceFiles();
                    } else {
                        alert('Error saving data! See details in console.');
                        console.log(e);
                    }
                }, this));
            }, this));
        }
    });
}

IgnisProject.prototype.buildAuraXExport = function (callback)
{
    if (this.timeline.length == 0) {
        callback(new Error('Timeline is empty.'));
        return;
    }

    this.prepareExport((data) => {
        this.ignis.timeline.deselect();
        this.sortTimeline();
        this.ignis.timeline.reindex();

        if (config.project.debug_export) {
            this.debug_export(data);
            callback(new Error('Debug export is enabled.'));
            return;
        }

        for (var i in this.timeline) {
            var n = this.timeline[i];
            if (n.type != 'effect' && n.mirror) {
                this.timeline[i].hash = this.getMirrorImageHash(n, this.leds);
            }
        }

        var restoreHashes = $.proxy(function () {
            for (var i in this.timeline) {
                var n = this.timeline[i];
                if (n.type != 'effect' && n.mirror) {
                    this.timeline[i].hash = this.timeline[i].hash.split('_')[0];
                }
            }
        }, this);

        try {
            processor_process_aurax(this, data, $.proxy(function (result) {
                restoreHashes();
                callback(null, result);
            }, this));
        } catch (e) {
            restoreHashes();
            callback(e);
        }
    });
}

IgnisProject.prototype.getAuraXUploadFilename = function ()
{
    if ($('#filename-editable-btn').hasClass('active')) {
        return this.sanitizeExportFilename($('#export-filename').val(), 'axp');
    }

    return this.getProjectFilename(true, 'axp') || this.sanitizeExportFilename($('#project-name').val(), 'axp');
}

IgnisProject.prototype.debug_export = function (data)
{
    console.log("DEBUG:");
    console.log(data);
    var debug_dir = ignis_dir('debug-export');
    for (var i in data) {
        fs.writeFileSync(debug_dir + path.sep + 'ignis_' + i + '.bin', data[i].data);
    }
}

IgnisProject.prototype.canJoinTimelineNodes = function (a, b)
{
    if (!a || !b) return false;
    if (a.start > b.end + 1 || a.end + 1 < b.start) return false;

    if (a.type == 'effect' || b.type == 'effect') {
        if (a.type != 'effect' || b.type != 'effect') return false;
        if (a.effectId != b.effectId) return false;
        if (parseInt(a.effectSpeed) != parseInt(b.effectSpeed)) return false;
        if (parseInt(a.effectIntensity) != parseInt(b.effectIntensity)) return false;
        if (parseInt(a.effectSize) != parseInt(b.effectSize)) return false;
        if (parseInt(a.effectPaletteId || 0) != parseInt(b.effectPaletteId || 0)) return false;
        if (!!a.effectRotate180 != !!b.effectRotate180) return false;
        if (!!a.effectFlipH != !!b.effectFlipH) return false;
        if (!!a.effectFlipV != !!b.effectFlipV) return false;
        if (!!a.mirror != !!b.mirror) return false;
        if (!!a.rotate != !!b.rotate) return false;
        if (!!a.reverse != !!b.reverse) return false;
        if (parseInt(a.mgap || 0) != parseInt(b.mgap || 0)) return false;
        var ac = a.effectColors || [];
        var bc = b.effectColors || [];
        if (ac.length != bc.length) return false;
        for (var i = 0; i < ac.length; i++) {
            if (String(ac[i]).toLowerCase() != String(bc[i]).toLowerCase()) return false;
        }
        return true;
    }

    return a.hash == b.hash && a.path == b.path;
}

IgnisProject.prototype.sanitizeExportFilename = function (filename)
{
    var ext = arguments.length > 1 ? arguments[1] : 'pix';
    ext = (ext == 'axp') ? 'axp' : 'pix';
    filename = path.basename((filename || '').toString());
    filename = remove_diacritics(filename).toLowerCase();
    filename = filename.replace(/[^a-zA-Z0-9._-]+/g, '-').replace(/-+/g, '-').replace(/^-|-$/g, '');

    if (filename.length == 0 || filename == '.' + ext) {
        filename = this.getProjectFilename(true, ext);
    }

    filename = filename.replace(/\.(pix|axp)$/i, '');
    filename = filename + '.' + ext;

    return filename;
}

IgnisProject.prototype.getExportTechnology = function ()
{
    return ($('#export-technology').val() == 'aurax') ? 'aurax' : 'photon';
}

IgnisProject.prototype.getExportTechnologyForFilename = function (filename)
{
    var ext = path.extname(filename || '').toLowerCase();
    if (ext == '.axp') return 'aurax';
    if (ext == '.pix') return 'photon';
    return this.getExportTechnology();
}

IgnisProject.prototype.getExportExtension = function ()
{
    return this.getExportTechnology() == 'aurax' ? 'axp' : 'pix';
}

IgnisProject.prototype.configureExportDialogFilters = function (preferredExt)
{
    var pix = { name: 'Photon / legacy .pix', extensions: ['pix'] };
    var axp = { name: 'AuraX compressed .axp', extensions: ['axp'] };
    var all = { name: 'All Files', extensions: ['*'] };

    this.dialog_export_properties.filters = (preferredExt == 'axp')
        ? [axp, pix, all]
        : [pix, axp, all];
}

IgnisProject.prototype.getProjectFilename = function (nonumber)
{
    var ext = arguments.length > 1 ? arguments[1] : 'pix';
    ext = (ext == 'axp') ? 'axp' : 'pix';

    nonumber = (nonumber ? true : false);

    var tln = '';

    if (this.timelinesCount() > 1) {
        var tln = 0;
        for (var hash in this.timelines) {
            if (hash == this.currentTimeline) break;
            tln++;
        }
        tln = '_' + (tln + 1);
    }

    var name = remove_diacritics($('#project-name').val()).toLowerCase();
    name = name.replace(/[^a-zA-Z0-9]+/g, ' ').trim().replace(/[ ]+/g, '-') + tln;

    if (nonumber) {
        return name + '.' + ext;
    }

    var drive = $('#drives').val();
    if (!drive) {
        return;
    }
    try {
        var files = fs.readdirSync(drive);
    } catch (e) {
        return;
    }
    var overwrite = false;
    var max_num = 0;
    for (var file of files) {
        if (!file.match(/\.(pix|axp)$/i)) continue;

        var m = file.match(/^([0-9]{3})_([a-zA-Z0-9_-]+)\.(pix|axp)$/);
        if (m) {
            var num = parseInt(m[1]);
            if (num > max_num) max_num = num;
            if (m[2] == name && m[3].toLowerCase() == ext) {
                name = file;
                overwrite = true;
            }
        }
    }

    if (name.length > 28) name = name.substring(0, 27);

    if (overwrite) {
        return name;
    }

    var num_str = ('' + (max_num + 1)).padStart(3, '0');

    return num_str + '_' + name + '.' + ext;
}

IgnisProject.prototype.prepareExport = function (callback)
{
    this.debug('prepare export');
    var imgs = {};
    var mimgs = {};
    var rimgs = {};
    var imgs_cnt = 0;

    for (var i = 0; i < this.timeline.length; i++) {
        var n = this.timeline[i];
        if (!n || n.type == 'effect') continue;
        if (n.mirror) {
            var mirrorKey = n.hash + '_' + n.mgap + '_' + (n.reverse ? 'v' : 'n');
            var mirrorData = { hash: n.hash, mgap: n.mgap, reverse: !!n.reverse };
            if (n.rotate) {
                if (rimgs[mirrorKey] == undefined) {
                    rimgs[mirrorKey] = mirrorData;
                    imgs_cnt++;
                }
            } else {
                if (mimgs[mirrorKey] == undefined) {
                    mimgs[mirrorKey] = mirrorData;
                    imgs_cnt++;
                }
            }
        } else {
            if (imgs[n.hash] == undefined) {
                imgs[n.hash] = n.path;
                imgs_cnt++;
            }
        }
    }

    this.proc.callback = callback;
    this.proc.imgs = imgs;
    this.proc.mimgs = mimgs;
    this.proc.rimgs = rimgs;
    this.proc.img_cnt = imgs_cnt;
    this.proc.resized = 0;
    this.proc.binarized = 0;
    this.proc.loaded = 0;

    var batch = [];

    for (var i in imgs) {
        var img = imgs[i];
        var img_path = ignis_dir('tmp') + path.sep + i + '.png';

        if (fs.existsSync(img_path)) fs.unlinkSync(img_path);

        batch.push({from: img, to: ignis_dir('tmp') + path.sep + i + '.png'});
        /*resize(img, ignis_dir('tmp') + path.sep + i + '.png', '', this.leds, $.proxy(function (e) {
            this.proc.resized++;
            if (this.proc.resized == this.proc.img_cnt) {
                this.prepareLoad();
                //project_proc.callback('DONE');
            }
        }, this));*/
    }

    if (batch.length == 0) {
        this.prepareMirrored();
        return;
    }

    this.debug('call:exportBatch');
    this.ignis.resizer.exportBatch(batch, this.leds, $.proxy(function () {
        //this.prepareLoad();
        this.debug('finished:exportBatch');
        this.prepareMirrored();
    }, this));
}

IgnisProject.prototype.prepareLoad = function ()
{
    this.debug('prepare load');
    var images = {};
    if (this.proc.img_cnt == 0) {
        this.prepareBinarize(images);
        return;
    }
    for (var i in this.proc.imgs) {
        images[i] = new Image();
        images[i].onload = $.proxy(function () {
            this.proc.loaded++;
            if (this.proc.loaded == this.proc.img_cnt) {
                this.prepareBinarize(images);
            }
        }, this);
        images[i].src = ignis_dir('tmp') + path.sep + i + '.png';
    }
    for (var i in this.proc.mimgs) {
        var mirror = this.proc.mimgs[i];
        var h = mirror.hash + '_m' + (mirror.reverse ? 'v' : '') + mirror.mgap + '_' + this.leds;
        images[h] = new Image();
        images[h].onload = $.proxy(function () {
            this.proc.loaded++;
            if (this.proc.loaded == this.proc.img_cnt) {
                this.prepareBinarize(images);
            }
        }, this);
        images[h].src = ignis_dir('mirror') + path.sep + h + '.png';
    }
    for (var i in this.proc.rimgs) {
        var mirror = this.proc.rimgs[i];
        var h = mirror.hash + '_r' + (mirror.reverse ? 'v' : '') + mirror.mgap + '_' + this.leds;
        images[h] = new Image();
        images[h].onload = $.proxy(function () {
            this.proc.loaded++;
            if (this.proc.loaded == this.proc.img_cnt) {
                this.prepareBinarize(images);
            }
        }, this);
        images[h].src = ignis_dir('mirror') + path.sep + h + '.png';
    }
}

IgnisProject.prototype.prepareMirrored = function ()
{
    this.debug('prepare mirrirored');
    var list = {};
    var mcnt = 0;
    var mcntf = 0;

    for (var i in this.timeline) {
        var n = this.timeline[i];
        if (n == null || n == undefined) continue;

        if (n.type == 'effect') continue;
        if (!n.mirror) continue;

        var h = this.getMirrorImageHash(n, this.leds);
        if (list[h]) continue;
        list[h] = { hash: n.hash, leds: this.leds, mgap: n.mgap, rotate: n.rotate, reverse: !!n.reverse };
        mcnt++;
    }

    var lcnt = 0;

    for (var i in list) {
        var n = list[i];
        this.ignis.resizer.stickMirrorGap(n.hash, n.leds, n.mgap, n.rotate, n.reverse, $.proxy(function () {
            mcntf++;
            if (mcntf >= mcnt) {
                this.debug('call:prepareLoad');
                this.prepareLoad();//this.prepareMirroredFinished();
            }
        }, this));
        lcnt++;
    }

    if (lcnt == 0) this.prepareLoad();
}

IgnisProject.prototype.prepareBinarize = function (images)
{
    this.debug('prepare binarize');
    var binaries = {};

    for (var i in images) {
        var img = images[i];
        var canvas = document.createElement('canvas');
        canvas.width = img.width;
        canvas.height = img.height;
        var ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0);
        var data = ctx.getImageData(0, 0, canvas.width, canvas.height);
        binaries[i] = data;
    }

    for (var i in this.timeline) {
        var node = this.timeline[i];
        if (!node || node.type != 'effect') continue;
        binaries[node.hash + ':' + node.uid] = this.generateEffectImageData(node);
    }

    this.proc.callback(binaries);
    this.proc = {};
}

IgnisProject.prototype.debug = function (val)
{
    //console.log(val);
}

IgnisProject.prototype.getNodeAt = function (time)
{
    for (var i in this.timeline) {
        var n = this.timeline[i];
        if (n.start <= time && n.end >= time) return n;
    }
    return false;
}

IgnisProject.prototype.effectColorToRgb = function (hex)
{
    hex = (hex || '#ffffff').replace('#', '');
    if (hex.length == 3) hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
    var value = parseInt(hex, 16);
    if (isNaN(value)) value = 0xffffff;
    return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255 };
}

IgnisProject.prototype.effectBlend = function (a, b, amount)
{
    amount = Math.max(0, Math.min(255, Math.trunc(amount)));
    return {
        r: Math.trunc(a.r + (b.r - a.r) * amount / 255) & 255,
        g: Math.trunc(a.g + (b.g - a.g) * amount / 255) & 255,
        b: Math.trunc(a.b + (b.b - a.b) * amount / 255) & 255
    };
}

IgnisProject.prototype.effectWheel = function (pos)
{
    pos = ((pos % 256) + 256) & 255;
    if (pos < 85) return { r: 255 - pos * 3, g: pos * 3, b: 0 };
    if (pos < 170) {
        pos -= 85;
        return { r: 0, g: 255 - pos * 3, b: pos * 3 };
    }
    pos -= 170;
    return { r: pos * 3, g: 0, b: 255 - pos * 3 };
}

IgnisProject.prototype.effectBuiltinPalette = function (paletteId, pos)
{
    pos = ((pos % 256) + 256) & 255;
    var blend = $.proxy(this.effectBlend, this);
    switch (parseInt(paletteId) || 0) {
        case 1: return this.effectWheel(pos);
        case 2:
            if (pos < 96) return blend({r:0,g:0,b:0}, {r:180,g:16,b:0}, pos * 255 / 95);
            if (pos < 190) return blend({r:180,g:16,b:0}, {r:255,g:120,b:0}, (pos - 96) * 255 / 93);
            return blend({r:255,g:120,b:0}, {r:255,g:240,b:160}, (pos - 190) * 255 / 65);
        case 3:
            return (pos < 170) ? blend({r:0,g:8,b:60}, {r:0,g:130,b:210}, pos * 255 / 169)
                               : blend({r:0,g:130,b:210}, {r:120,g:255,b:220}, (pos - 170) * 255 / 85);
        case 4:
            return (pos < 180) ? blend({r:0,g:24,b:0}, {r:0,g:140,b:36}, pos * 255 / 179)
                               : blend({r:0,g:140,b:36}, {r:220,g:255,b:80}, (pos - 180) * 255 / 75);
        case 5: {
            var colors = [{r:255,g:0,b:80}, {r:0,g:220,b:255}, {r:255,g:220,b:0}, {r:110,g:0,b:255}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 6:
            return (pos < 150) ? blend({r:60,g:0,b:80}, {r:255,g:72,b:0}, pos * 255 / 149)
                               : blend({r:255,g:72,b:0}, {r:255,g:190,b:70}, (pos - 150) * 255 / 105);
        case 7:
            return (pos < 170) ? blend({r:0,g:40,b:120}, {r:120,g:230,b:255}, pos * 255 / 169)
                               : blend({r:120,g:230,b:255}, {r:255,g:255,b:255}, (pos - 170) * 255 / 85);
        case 8:
            if (pos < 88) return blend({r:0,g:0,b:0}, {r:160,g:0,b:0}, pos * 255 / 87);
            if (pos < 180) return blend({r:160,g:0,b:0}, {r:255,g:70,b:0}, (pos - 88) * 255 / 91);
            return blend({r:255,g:70,b:0}, {r:255,g:220,b:120}, (pos - 180) * 255 / 75);
        case 9: {
            var colors = [{r:255,g:132,b:192}, {r:124,g:255,b:190}, {r:132,g:190,b:255}, {r:255,g:232,b:120}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 10: {
            var colors = [{r:255,g:0,b:220}, {r:0,g:255,b:255}, {r:130,g:255,b:0}, {r:255,g:60,b:0}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 11: {
            var colors = [{r:255,g:40,b:110}, {r:255,g:255,b:255}, {r:80,g:210,b:255}, {r:255,g:240,b:110}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 12: {
            var colors = [{r:24,g:16,b:100}, {r:0,g:220,b:170}, {r:160,g:80,b:255}, {r:20,g:255,b:80}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 13: {
            var colors = [{r:80,g:20,b:10}, {r:220,g:120,b:36}, {r:255,g:218,b:150}, {r:20,g:90,b:95}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 14:
            return this.effectWheel((pos & 0x20) ? pos : pos + 40);
        case 15: {
            var colors = [{r:0,g:12,b:80}, {r:0,g:120,b:255}, {r:150,g:0,b:255}, {r:255,g:40,b:210}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 16: {
            var colors = [{r:255,g:0,b:92}, {r:255,g:180,b:220}, {r:255,g:255,b:255}, {r:120,g:220,b:255}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 17: {
            var colors = [{r:255,g:0,b:0}, {r:255,g:160,b:0}, {r:0,g:180,b:70}, {r:0,g:80,b:255}];
            return colors[(pos >> 6) & 3];
        }
        case 18: {
            var colors = [{r:18,g:0,b:70}, {r:0,g:180,b:190}, {r:255,g:40,b:120}, {r:255,g:180,b:40}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 19: {
            var colors = [{r:255,g:160,b:70}, {r:255,g:230,b:150}, {r:40,g:180,b:255}, {r:0,g:30,b:120}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 20:
            return (pos < 128) ? blend({r:255,g:0,b:0}, {r:0,g:70,b:255}, pos * 2)
                               : blend({r:0,g:70,b:255}, {r:255,g:0,b:0}, (pos - 128) * 2);
        case 21:
            return (pos < 128) ? blend({r:255,g:220,b:0}, {r:0,g:255,b:70}, pos * 2)
                               : blend({r:0,g:255,b:70}, {r:255,g:220,b:0}, (pos - 128) * 2);
        case 22:
            return (pos < 128) ? blend({r:120,g:0,b:255}, {r:0,g:255,b:100}, pos * 2)
                               : blend({r:0,g:255,b:100}, {r:120,g:0,b:255}, (pos - 128) * 2);
        case 23:
            return (pos < 128) ? blend({r:255,g:120,b:40}, {r:255,g:235,b:180}, pos * 2)
                               : blend({r:255,g:235,b:180}, {r:255,g:120,b:40}, (pos - 128) * 2);
        case 24: {
            var colors = [{r:0,g:255,b:210}, {r:0,g:80,b:255}, {r:255,g:0,b:220}, {r:255,g:255,b:255}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 25:
            return (pos < 128) ? blend({r:255,g:0,b:0}, {r:255,g:255,b:255}, pos * 2)
                               : blend({r:255,g:255,b:255}, {r:0,g:80,b:255}, (pos - 128) * 2);
        case 26: {
            var colors = [{r:0,g:20,b:0}, {r:0,g:255,b:70}, {r:186,g:255,b:128}, {r:0,g:80,b:20}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 27: {
            var colors = [{r:255,g:45,b:133}, {r:255,g:210,b:232}, {r:255,g:255,b:255}, {r:180,g:20,b:90}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 28: {
            var colors = [{r:0,g:20,b:255}, {r:0,g:240,b:255}, {r:255,g:255,b:255}, {r:0,g:90,b:180}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        case 29: {
            var colors = [{r:255,g:138,b:0}, {r:255,g:224,b:110}, {r:0,g:180,b:170}, {r:0,g:55,b:80}];
            var band = pos >> 6;
            return blend(colors[band], colors[(band + 1) & 3], (pos & 0x3F) * 4);
        }
        default:
            return this.effectWheel(pos);
    }
}

IgnisProject.prototype.effectPaletteAt = function (node, pos, colors)
{
    pos = ((pos % 256) + 256) & 255;
    if (parseInt(node.effectPaletteId) > 0) return this.effectBuiltinPalette(node.effectPaletteId, pos);
    colors = colors && colors.length ? colors : [{r:255,g:96,b:0}];
    var slots = this.ignis.library.getEffectColorSlots(node.effectId);
    if (slots <= 0) return this.effectWheel(pos);
    colors = colors.slice(0, slots);
    if (colors.length <= 1) return colors[0];
    var scaled = pos * colors.length;
    var idx = (scaled >> 8) % colors.length;
    var next = (idx + 1) % colors.length;
    return this.effectBlend(colors[idx], colors[next], scaled & 255);
}

IgnisProject.prototype.effectLakeColor = function (node, pos, colors)
{
    if ((node.effectColors || []).length > 1 || parseInt(node.effectPaletteId) > 0) {
        return this.effectPaletteAt(node, pos, colors);
    }
    return (pos < 160) ? this.effectBlend({r:0,g:16,b:72}, {r:0,g:170,b:255}, pos * 255 / 160)
                       : this.effectBlend({r:0,g:170,b:255}, {r:120,g:255,b:210}, (pos - 160) * 255 / 95);
}

IgnisProject.prototype.effectSignature = function (node, leds, maxColumns)
{
    var data = {
        renderer: 4,
        id: node.effectId || node.id || node.hash,
        speed: node.effectSpeed || node.speed,
        intensity: node.effectIntensity || node.intensity,
        size: node.effectSize || node.size,
        palette: (node.effectPaletteId !== undefined && node.effectPaletteId !== null) ? node.effectPaletteId : node.paletteId,
        colors: node.effectColors || node.colors,
        duration: node.duration,
        frequency: node.frequency || 40,
        mirror: !!node.mirror,
        rotate: !!node.rotate,
        reverse: !!node.reverse,
        effectRotate180: !!node.effectRotate180,
        effectFlipH: !!node.effectFlipH,
        effectFlipV: !!node.effectFlipV,
        mgap: node.mgap || 0,
        leds: leds || this.leds,
        maxColumns: maxColumns || 0
    };
    return window.electronApi.md5(JSON.stringify(data));
}

IgnisProject.prototype.effectPreviewDataUrl = function (node, width, height, zoomed)
{
    var opts = (zoomed && typeof zoomed == 'object') ? zoomed : {};
    var ledCount = Math.max(1, parseInt(opts.leds || this.leds || config.project.default_leds || 170));
    var previewScale = Math.max(1, Math.min(4, parseInt(opts.previewScale || 4)));
    var previewW = Math.max(32, parseInt(width || 100));
    var previewH = Math.max(32, parseInt(height || 100));
    var columns = Math.max(1, Math.round(ledCount * previewW / previewH));
    var previewNode = $.extend(true, {}, node, { duration: 2200, frequency: 80 });
    var image = this.generateEffectImageData(previewNode, { leds: ledCount, columns: columns * previewScale, previewScale: previewScale });
    var canvas = document.createElement('canvas');
    canvas.width = previewW;
    canvas.height = previewH;
    var ctx = canvas.getContext('2d');
    var src = document.createElement('canvas');
    src.width = image.width;
    src.height = image.height;
    var srcCtx = src.getContext('2d');
    var data = srcCtx.createImageData(image.width, image.height);
    data.data.set(image.data);
    srcCtx.putImageData(data, 0, 0);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(src, 0, 0, image.width, image.height, 0, 0, previewW, previewH);
    return canvas.toDataURL('image/png');
}

IgnisProject.prototype.ensureEffectTexture = function (node, leds)
{
    leds = Math.max(1, parseInt(leds || this.leds || config.project.default_leds || 170));
    var maxColumns = 2048;
    var sig = this.effectSignature(node, leds, maxColumns);
    var filePath = ignis_texdir() + path.sep + 'effect_' + sig + '_' + leds + '.png';
    if (!fs.existsSync(filePath)) {
        var image = this.generateEffectImageData(node, { leds: leds, maxColumns: maxColumns, previewScale: 4 });
        var canvas = document.createElement('canvas');
        canvas.width = image.width;
        canvas.height = image.height;
        var ctx = canvas.getContext('2d');
        var data = ctx.createImageData(image.width, image.height);
        data.data.set(image.data);
        ctx.putImageData(data, 0, 0);
        var png = canvas.toDataURL('image/png').replace(/^data:image\/png;base64,/, '');
        fs.writeFileSync(filePath, Buffer.from(png, 'base64'));
        node._effectTextureRatio = this.getEffectTextureRatio(node, leds, maxColumns);
    }
    return {
        path: filePath.replace(/\\/g, '/'),
        ratio: node._effectTextureRatio || this.getEffectTextureRatio(node, leds, maxColumns),
        signature: sig
    };
}

IgnisProject.prototype.getEffectTextureRatio = function (node, leds, maxColumns)
{
    var frequency = Math.max(1, parseInt(node.frequency || 40));
    var cols = Math.max(1, Math.ceil((node.duration || 1000) * frequency / 1000));
    if (maxColumns && cols > maxColumns) cols = maxColumns;
    return cols / Math.max(1, parseInt(leds || this.leds || config.project.default_leds || 170));
}

IgnisProject.prototype.cloneEffectImageData = function (image)
{
    return {
        width: image.width,
        height: image.height,
        data: new Uint8ClampedArray(image.data)
    };
}

IgnisProject.prototype.copyEffectPixel = function (source, target, sx, sy, dx, dy)
{
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0) return;
    if (sx >= source.width || sy >= source.height || dx >= target.width || dy >= target.height) return;
    var sp = (sy * source.width + sx) * 4;
    var dp = (dy * target.width + dx) * 4;
    target.data[dp] = source.data[sp];
    target.data[dp + 1] = source.data[sp + 1];
    target.data[dp + 2] = source.data[sp + 2];
    target.data[dp + 3] = source.data[sp + 3];
}

IgnisProject.prototype.applyEffectMirror = function (image, node)
{
    var gap = parseInt(node.mgap || 0);
    if (isNaN(gap) || gap < 0) gap = 0;
    if (gap % 2 != 0) gap += 1;
    if (gap >= image.height) gap = 0;

    var half = Math.max(1, Math.floor((image.height - gap) / 2));
    var top = {
        width: image.width,
        height: half,
        data: new Uint8ClampedArray(image.width * half * 4)
    };
    var out = {
        width: image.width,
        height: image.height,
        data: new Uint8ClampedArray(image.width * image.height * 4)
    };
    for (var x = 0; x < image.width; x++) {
        for (var y = 0; y < half; y++) {
            var sy = Math.min(image.height - 1, Math.floor(y * image.height / half));
            this.copyEffectPixel(image, top, x, sy, x, y);
        }
    }
    for (var i = 3; i < out.data.length; i += 4) out.data[i] = 255;

    for (var x = 0; x < image.width; x++) {
        for (var y = 0; y < half; y++) {
            var bottomSrcY = node.rotate ? y : (half - 1 - y);
            var bottomSrcX = node.rotate ? (image.width - 1 - x) : x;
            if (node.reverse) {
                this.copyEffectPixel(top, out, bottomSrcX, bottomSrcY, x, y);
                this.copyEffectPixel(top, out, x, y, x, half + gap + y);
            } else {
                this.copyEffectPixel(top, out, x, y, x, y);
                this.copyEffectPixel(top, out, bottomSrcX, bottomSrcY, x, half + gap + y);
            }
        }
    }

    return out;
}

IgnisProject.prototype.applyEffectReverse = function (image)
{
    var out = {
        width: image.width,
        height: image.height,
        data: new Uint8ClampedArray(image.data.length)
    };
    for (var x = 0; x < image.width; x++) {
        for (var y = 0; y < image.height; y++) {
            this.copyEffectPixel(image, out, x, y, x, image.height - 1 - y);
        }
    }
    return out;
}

IgnisProject.prototype.applyEffectFlipH = function (image)
{
    var out = {
        width: image.width,
        height: image.height,
        data: new Uint8ClampedArray(image.data.length)
    };
    for (var x = 0; x < image.width; x++) {
        for (var y = 0; y < image.height; y++) {
            this.copyEffectPixel(image, out, x, y, image.width - 1 - x, y);
        }
    }
    return out;
}

IgnisProject.prototype.applyEffectTransforms = function (image, node)
{
    var out = image;
    if (node && node.effectRotate180) {
        out = this.applyEffectFlipH(out);
        out = this.applyEffectReverse(out);
    } else {
        if (node && node.effectFlipH) out = this.applyEffectFlipH(out);
        if (node && node.effectFlipV) out = this.applyEffectReverse(out);
    }
    if (node && node.mirror) out = this.applyEffectMirror(out, node);
    if (node && node.reverse) out = this.applyEffectReverse(out);
    return out;
}

IgnisProject.prototype.generateEffectImageData = function (node, options)
{
    options = options || {};
    var logicalLeds = Math.max(1, parseInt(options.leds || this.leds || config.project.default_leds || 170));
    var previewScale = Math.max(1, Math.min(4, parseInt(options.previewScale || 1)));
    var leds = logicalLeds * previewScale;
    if (previewScale > 1) {
        node = $.extend(true, {}, node);
        if (node.size !== undefined) node.size = Math.max(1, Math.round(parseInt(node.size || 1) * previewScale));
        if (node.effectSize !== undefined) node.effectSize = Math.max(1, Math.round(parseInt(node.effectSize || 1) * previewScale));
        if (node.mgap !== undefined) node.mgap = Math.max(0, Math.round(parseInt(node.mgap || 0) * previewScale));
    }
    var frequency = Math.max(1, Math.min(parseInt(node.frequency || 40), config.project.max_line_frequency || 2500));
    var fullCols = Math.max(1, Math.ceil((node.duration || 1000) * frequency / 1000));
    var cols = options.columns ? parseInt(options.columns) : fullCols * previewScale;
    if (options.maxColumns && cols > options.maxColumns) cols = options.maxColumns;
    cols = Math.max(1, cols);

    var image = { width: cols, height: leds, data: new Uint8ClampedArray(cols * leds * 4) };
    var effectId = this.ignis.library.normalizeEffectId(node.effectId || node.id || node.hash);
    var colors = ((node.effectColors && node.effectColors.length) ? node.effectColors : (node.colors && node.colors.length ? node.colors : ['#ff6000', '#00b4ff', '#ffffff'])).map(this.effectColorToRgb);
    var speed = Math.max(10, Math.min(parseInt(node.effectSpeed || node.speed || 100), 1000));
    var inten = Math.max(0, Math.min(parseInt(node.effectIntensity || node.intensity || 128), 255));
    var size = Math.max(1, Math.min(parseInt(node.effectSize || node.size || 3), leds));
    if (node.effectPaletteId === undefined && node.paletteId !== undefined) node.effectPaletteId = node.paletteId;
    var phase = 0;
    var rng = 0x1234abcd ^ ((leds << 16) >>> 0);
    var rippleOrigin = Math.floor(leds / 2);
    var rippleRadius = 0;
    var androidPos = 0;
    var androidWidth = 2;
    var androidPhase = 0;
    var androidCall = 0;
    var androidRenderedCall = -1;
    var buffer = new Uint8ClampedArray(leds * 3);

    var that = this;
    function u8(x) { return Math.trunc(x) & 255; }
    function u16(x) { return Math.max(0, Math.min(65535, Math.trunc(x))); }
    function clear() { buffer.fill(0); }
    function fade(keep) {
        keep = u8(keep);
        for (var i = 0; i < leds; i++) {
            var p = i * 3;
            buffer[p] = Math.trunc(buffer[p] * keep / 255);
            buffer[p + 1] = Math.trunc(buffer[p + 1] * keep / 255);
            buffer[p + 2] = Math.trunc(buffer[p + 2] * keep / 255);
        }
    }
    function setPixel(i, c, scale) {
        if (i < 0 || i >= leds) return;
        scale = u8(scale);
        var p = i * 3;
        buffer[p] = Math.trunc(c.r * scale / 255);
        buffer[p + 1] = Math.trunc(c.g * scale / 255);
        buffer[p + 2] = Math.trunc(c.b * scale / 255);
    }
    function addPixel(i, c, scale) {
        if (i < 0 || i >= leds) return;
        scale = u8(scale);
        var p = i * 3;
        buffer[p] = Math.min(255, buffer[p] + Math.trunc(c.r * scale / 255));
        buffer[p + 1] = Math.min(255, buffer[p + 1] + Math.trunc(c.g * scale / 255));
        buffer[p + 2] = Math.min(255, buffer[p + 2] + Math.trunc(c.b * scale / 255));
    }
    function wave8(x) {
        var v = ((Math.floor(x) % 256) + 256) & 255;
        return v < 128 ? v << 1 : 255 - ((v - 128) << 1);
    }
    function hash8(x, y) {
        var h = (Math.imul(x, 374761393) + Math.imul(y, 668265263) + 0x9e3779b9) >>> 0;
        h = Math.imul((h ^ (h >>> 13)) >>> 0, 1274126177) >>> 0;
        return (h >>> 24) & 255;
    }
    function noise8(x, t) {
        x = u16(x);
        t = u16(t);
        var cell = x >> 4;
        var frac = (x & 0x0F) << 4;
        var a = hash8(cell, t >> 4);
        var b = hash8(cell + 1, t >> 4);
        return u8(a + ((b - a) * frac / 255));
    }
    function circularDistance(a, b) {
        var d = Math.abs(a - b);
        var wrap = leds - d;
        return d < wrap ? d : wrap;
    }
    function randomLed() {
        rng ^= (rng << 13); rng >>>= 0;
        rng ^= (rng >>> 17); rng >>>= 0;
        rng ^= (rng << 5); rng >>>= 0;
        return leds ? rng % leds : 0;
    }
    function drawBlob(center, width, c, scale) {
        width = Math.max(1, width);
        for (var i = 0; i < leds; i++) {
            var d = circularDistance(i, center);
            if (d > width) continue;
            addPixel(i, c, (width - d + 1) * scale / (width + 1));
        }
    }
    function palette(pos) { return that.effectPaletteAt(node, pos, colors); }
    function lake(pos) { return that.effectLakeColor(node, pos, colors); }
    function speedStep() { return 1 + Math.floor(speed / 7); }
    function renderAndroidCall() {
        var maxWidth = Math.max(1, Math.min(size, leds));
        var fg = colors[0] || palette(0);
        var bg = colors[1] || { r: 0, g: 0, b: 0 };
        clear();
        if (colors[1]) {
            for (var i = 0; i < leds; i++) setPixel(i, bg, 255);
        }

        if (androidWidth > maxWidth) androidPhase = 1;
        else if (androidWidth < 2) androidPhase = 0;

        var a = androidPos;
        if (androidPhase == 0) {
            if (androidCall % 3 == 1) a++;
            else androidWidth++;
        } else {
            a++;
            if (androidCall % 3 != 1 && androidWidth > 0) androidWidth--;
        }
        if (a >= leds) a = 0;

        if (a + androidWidth <= leds) {
            for (var i = a; i < a + androidWidth; i++) setPixel(i, fg, 255);
        } else {
            for (var i = a; i < leds; i++) setPixel(i, fg, 255);
            var tail = androidWidth - (leds - a);
            for (var i = 0; i < tail; i++) setPixel(i, fg, 255);
        }
        androidPos = a;
        androidCall++;
    }

    for (var x = 0; x < cols; x++) {
        var frame = x * fullCols / cols;
        var frameIndex = Math.floor(frame);
        phase = frame * speedStep();

        switch (effectId) {
            case 1:
                clear();
                for (var i = 0; i < leds; i++) setPixel(i, palette(0), 255);
                break;
            case 2: {
                var androidFps = Math.max(1, speed / 5);
                var targetCall = Math.floor(frame * androidFps / frequency);
                while (androidRenderedCall < targetCall) {
                    renderAndroidCall();
                    androidRenderedCall++;
                }
                break;
            }
            case 10:
                clear();
                for (var i = 0; i < leds; i++) {
                    var pos = u8(i * 255 / leds + (phase >> 2));
                    var beat = wave8(i * 19 + (phase << 1));
                    setPixel(i, palette(pos), 48 + beat * (96 + Math.trunc(inten / 2)) / 255);
                }
                break;
            case 11:
                clear();
                for (var i = 0; i < leds; i++) {
                    var wave = wave8(i * (8 + Math.trunc(inten / 24)) + phase);
                    var pos = u8(i * 255 / leds + phase + Math.trunc(wave / 3));
                    setPixel(i, palette(pos), 70 + wave * 185 / 255);
                }
                break;
            case 12: {
                clear();
                var center = Math.floor(leds / 2);
                var maxRadius = Math.floor((leds + 1) / 2);
                var radius = Math.trunc(wave8(phase >> 1) * maxRadius / 255);
                for (var i = 0; i < leds; i++) {
                    var d = Math.abs(i - center);
                    var diff = Math.abs(d - radius);
                    if (diff > size) continue;
                    addPixel(i, palette(phase + d * 14), (size - diff + 1) * 255 / (size + 1));
                }
                break;
            }
            case 13: {
                clear();
                var freq = 2 + Math.trunc(inten / 32);
                var center = Math.floor(leds / 2);
                for (var i = 0; i < leds; i++) {
                    var d = Math.abs(i - center);
                    var v = wave8(d * freq * 9 + phase);
                    setPixel(i, palette(phase / 2 + d * 20), v < 120 ? 0 : (v - 120) * 255 / 135);
                }
                break;
            }
            case 14:
            case 15: {
                clear();
                var count = effectId == 14 ? 2 : 3;
                var tail = Math.max(1, size || (count == 2 ? 8 : 6));
                var base = (phase >> 3) % leds;
                for (var h = 0; h < count; h++) {
                    var head = Math.floor(base + h * leds / count) % leds;
                    var c = palette(h * 255 / count + phase);
                    for (var i = 0; i < leds; i++) {
                        var dist = (head + leds - i) % leds;
                        if (dist > tail) continue;
                        addPixel(i, c, (tail - dist + 1) * 255 / (tail + 1));
                    }
                }
                break;
            }
            case 16: {
                clear();
                var width = Math.max(1, size);
                var spacing = width * 3 + 2;
                var base = (phase >> 3) % leds;
                for (var i = 0; i < 4; i++) drawBlob((base + i * spacing) % leds, width, palette(i * 64 + phase), 235);
                break;
            }
            case 17:
                clear();
                for (var i = 0; i < leds; i++) {
                    var a = wave8(i * 8 + (phase >> 1));
                    var b = wave8(i * 17 - (phase >> 2));
                    var v = (a + b) / 2;
                    setPixel(i, lake(v), 50 + v * 205 / 255);
                }
                break;
            case 18: {
                fade(165 + inten / 4);
                var tail = Math.max(1, size || 10);
                var head = (phase >> 4) % leds;
                var c = palette(phase >> 1);
                for (var d = 0; d <= tail; d++) addPixel((head + leds - d) % leds, c, (tail - d + 1) * 255 / (tail + 1));
                break;
            }
            case 19:
                clear();
                var scale = 10 + inten / 7;
                for (var i = 0; i < leds; i++) {
                    var n1 = noise8(i * scale, phase >> 1);
                    var n2 = noise8(i * (scale / 2 + 5) + 71, phase >> 2);
                    var v = (n1 + n2) / 2;
                    setPixel(i, palette(v), v);
                }
                break;
            case 20:
                clear();
                for (var i = 0; i < 3; i++) {
                    var w = wave8((phase >> (i == 0 ? 1 : 2)) + i * 85);
                    var center = Math.floor(w * Math.max(1, leds - 1) / 255);
                    drawBlob(center, size || 5, palette(i * 85 + phase), 240);
                }
                break;
            case 21: {
                clear();
                var maxRadius = Math.floor(leds / 2) + size + 1;
                if (rippleRadius == 0 || rippleRadius > maxRadius) {
                    rippleOrigin = randomLed();
                    rippleRadius = 1;
                }
                var c = palette(frameIndex * 17);
                for (var i = 0; i < leds; i++) {
                    var d = circularDistance(i, rippleOrigin);
                    var diff = Math.abs(d - rippleRadius);
                    if (diff > size) continue;
                    addPixel(i, c, (size - diff + 1) * 255 / (size + 1));
                }
                rippleRadius += 1 + (speed > 500 ? 1 : 0);
                break;
            }
            case 22:
                clear();
                for (var i = 0; i < leds; i++) {
                    var v = wave8(i * (8 + inten / 28) + phase);
                    setPixel(i, palette(phase + i * 8), v < 80 ? 0 : (v - 80) * 255 / 175);
                }
                break;
            case 23: {
                var cycle = 24 + Math.trunc((1010 - speed) / 18);
                var onTime = 2 + Math.trunc(inten * (Math.trunc(cycle / 2) + 1) / 255);
                clear();
                if ((frameIndex % cycle) < onTime) {
                    var c = palette(phase >> 1);
                    for (var i = 0; i < leds; i++) setPixel(i, c, 255);
                }
                break;
            }
            case 24: {
                clear();
                var c = palette(phase >> 1);
                var wave = wave8(phase);
                var minBri = 20 + Math.trunc(inten / 6);
                var bri = minBri + wave * (255 - minBri) / 255;
                for (var i = 0; i < leds; i++) setPixel(i, c, bri);
                break;
            }
            case 25:
                clear();
                var spread = 4 + inten / 8;
                for (var i = 0; i < leds; i++) setPixel(i, that.effectBuiltinPalette(1, i * spread + (phase >> 1)), 255);
                break;
            case 26: {
                clear();
                var density = 20 + Math.trunc(inten / 2);
                var tick = phase >> 5;
                for (var i = 0; i < leds; i++) {
                    var h = hash8(i * 17, tick);
                    if (h > density) continue;
                    var age = hash8(i * 23, tick + 19);
                    var bri = age < 128 ? age * 2 : (255 - age) * 2;
                    setPixel(i, palette(h + phase), bri);
                }
                break;
            }
            case 27: {
                fade(150 + inten / 4);
                var count = 1 + Math.trunc(inten / 32);
                for (var n = 0; n < count; n++) setPixel(randomLed(), palette(phase + n * 37), 255);
                break;
            }
            case 28: {
                fade(170);
                var bursts = 2 + Math.trunc(inten / 70);
                var span = leds || 1;
                var width = Math.max(1, size || 4);
                for (var b = 0; b < bursts; b++) {
                    var local = (phase >> 2) + b * 73;
                    var origin = Math.trunc(hash8(b * 41, local >> 6) * span / 255);
                    var radius = wave8(local);
                    var centerA = (origin + Math.trunc(radius * span / 510)) % span;
                    var centerB = (origin + span - Math.trunc(radius * span / 510)) % span;
                    var c = palette(b * 70 + phase);
                    drawBlob(centerA, width, c, 210);
                    drawBlob(centerB, width, c, 210);
                }
                break;
            }
            case 29:
            case 30: {
                fade(110 + inten / 3);
                var width = Math.max(1, size || 4);
                var span = Math.max(1, leds - 1);
                var center = Math.trunc(wave8(phase >> 1) * span / 255);
                drawBlob(center, width, palette(phase >> 1), 255);
                if (effectId == 30) drawBlob(span - center, width, palette(phase + 128), 255);
                break;
            }
            case 31: {
                clear();
                var spacing = 3 + Math.trunc(inten / 42);
                var duty = 1 + (Math.max(1, size || 2) % spacing);
                var offset = (phase >> 4) % spacing;
                for (var i = 0; i < leds; i++) {
                    var slot = (i + offset) % spacing;
                    if (slot < duty) setPixel(i, palette(i * 12 + phase), 255);
                }
                break;
            }
            case 32: {
                clear();
                var span = leds || 1;
                var head = (phase >> 4) % (span + 1);
                var c = palette(phase >> 2);
                for (var i = 0; i < head && i < leds; i++) setPixel(i, c, 255);
                break;
            }
            case 33: {
                fade(120 + inten / 3);
                var dots = 3 + Math.trunc(inten / 43);
                var span = Math.max(1, leds - 1);
                for (var d = 0; d < dots; d++) {
                    var pos = Math.trunc(wave8((phase >> 1) + d * 37) * span / 255);
                    addPixel(pos, palette(d * 255 / dots + phase), 230);
                }
                break;
            }
            case 34: {
                fade(135 + inten / 4);
                var width = Math.max(1, size || 3);
                var span = Math.max(1, leds - 1);
                var pos = Math.trunc(wave8(phase >> 1) * span / 255);
                drawBlob(pos, width, palette(phase >> 1), 255);
                break;
            }
            case 35:
                clear();
                var cooling = 10 + Math.trunc((255 - inten) / 5);
                for (var i = 0; i < leds; i++) {
                    var heat = noise8(i * 18, phase >> 1);
                    var shaped = (heat * heat) >> 8;
                    if (i > leds / 2) shaped = shaped > cooling ? shaped - cooling : 0;
                    setPixel(i, that.effectBuiltinPalette(node.effectPaletteId ? node.effectPaletteId : 2, shaped), shaped);
                }
                break;
            case 36:
                clear();
                var spread = 8 + inten / 10;
                for (var i = 0; i < leds; i++) {
                    var a = wave8(i * spread + phase);
                    var b = wave8(i * (spread / 2 + 7) - (phase >> 1));
                    var c = wave8(i * 3 + (phase >> 2));
                    var v = (a + b + c) / 3;
                    setPixel(i, palette(v), 255);
                }
                break;
            case 37:
                clear();
                var spread = 2 + inten / 10;
                for (var i = 0; i < leds; i++) setPixel(i, palette(i * spread + (phase >> 2)), 255);
                break;
            case 38: {
                clear();
                var wave = wave8(phase >> 1);
                var floor = Math.trunc(inten / 8);
                var bri = floor + wave * (255 - floor) / 255;
                var c = palette(phase >> 3);
                for (var i = 0; i < leds; i++) setPixel(i, c, bri);
                break;
            }
            case 39: {
                clear();
                var count = 2 + Math.trunc(inten / 28);
                var width = Math.max(1, size || 2);
                var span = leds || 1;
                for (var d = 0; d < count; d++) {
                    var pos = (Math.trunc((phase >> 3) * (d + 1)) + Math.trunc(d * span / count)) % span;
                    drawBlob(pos, width, palette(d * 255 / count + phase), 230);
                }
                break;
            }
            case 40: {
                clear();
                var tail = Math.max(1, size || 8);
                var span = leds || 1;
                var a = (phase >> 3) % span;
                var b = (span - 1) - a;
                for (var d = 0; d <= tail; d++) {
                    var bri = (tail - d + 1) * 255 / (tail + 1);
                    addPixel((a + span - d) % span, palette(phase), bri);
                    addPixel((b + d) % span, palette(phase + 128), bri);
                }
                break;
            }
            case 41: {
                clear();
                var tail = Math.max(1, size || 6);
                var half = Math.floor(leds / 2);
                var span = half || 1;
                var p0 = (phase >> 3) % span;
                for (var d = 0; d <= tail; d++) {
                    var bri = (tail - d + 1) * 255 / (tail + 1);
                    addPixel((p0 + span - d) % span, palette(phase), bri);
                    addPixel(half + ((span - 1 - p0 + d) % span), palette(phase + 96), bri);
                }
                break;
            }
            case 42: {
                clear();
                var width = Math.max(1, size || 4);
                var span = Math.max(1, leds - 1);
                var pos = Math.trunc(wave8(phase >> 1) * span / 255);
                drawBlob(pos, width, palette(phase), 255);
                drawBlob(span - pos, width, palette(phase + 128), 255);
                break;
            }
            case 43:
                clear();
                var width = 12 + Math.trunc(inten / 5);
                for (var i = 0; i < leds; i++) {
                    var v = (i * width + phase) & 255;
                    setPixel(i, palette(v), v);
                }
                break;
            case 44:
                clear();
                var center = Math.floor(leds / 2);
                var width = 12 + Math.trunc(inten / 6);
                for (var i = 0; i < leds; i++) {
                    var d = Math.abs(i - center);
                    var v = wave8(d * width + phase);
                    setPixel(i, palette(d * 12 + phase), v);
                }
                break;
            case 45: {
                clear();
                var width = Math.max(1, size || 3);
                var count = 2 + Math.trunc(inten / 40);
                var span = leds || 1;
                for (var k = 0; k < count; k++) {
                    var pos = ((phase >> 3) + Math.trunc(k * span / count)) % span;
                    drawBlob(pos, width, palette(k * 255 / count + phase), 255);
                }
                break;
            }
            case 46:
                clear();
                var freq = 8 + Math.trunc(inten / 18);
                for (var i = 0; i < leds; i++) {
                    var a = wave8(i * freq + phase);
                    var b = wave8(i * freq - phase);
                    var v = (a + b) / 2;
                    setPixel(i, palette(i * 9 + phase), v);
                }
                break;
            case 47:
                clear();
                var bands = 18 + Math.trunc(inten / 8);
                for (var i = 0; i < leds; i++) {
                    var v = (i * bands + (phase >> 1)) & 255;
                    setPixel(i, palette(v), (v & 0x7F) < 64 ? 255 : 70);
                }
                break;
            case 48: {
                fade(90 + inten / 2);
                var width = Math.max(1, size || 2);
                var bars = 2 + Math.trunc(inten / 64);
                var span = leds || 1;
                for (var b = 0; b < bars; b++) {
                    var pos = (Math.trunc((phase >> 2) * (b + 1)) + Math.trunc(b * span / bars)) % span;
                    drawBlob(pos, width, palette(phase + b * 70), 255);
                }
                break;
            }
            case 49:
                clear();
                var spread = 6 + Math.trunc(inten / 14);
                for (var i = 0; i < leds; i++) {
                    var pos = i * spread + (phase >> 1);
                    var gate = wave8(i * 11 - (phase >> 2));
                    setPixel(i, palette(pos), 70 + gate * 185 / 255);
                }
                break;
            case 50:
                clear();
                var width = Math.max(1, size || 5);
                var span = leds || 1;
                var pos = (phase >> 2) % span;
                drawBlob(pos, width, palette(phase), 255);
                drawBlob((pos + Math.floor(span / 3)) % span, width, palette(phase + 85), 220);
                drawBlob((pos + Math.floor(2 * span / 3)) % span, width, palette(phase + 170), 220);
                break;
            case 51:
                clear();
                var density = 10 + Math.trunc(inten / 9);
                for (var i = 0; i < leds; i++) {
                    var a = wave8(i * density + phase);
                    var b = wave8(i * (density + 9) - (phase >> 1));
                    var v = Math.abs(a - b);
                    setPixel(i, palette(phase + i * 13), v);
                }
                break;
            case 52: {
                clear();
                var tail = Math.max(1, size || 10);
                var span = leds || 1;
                var head = (phase >> 3) % span;
                var c = palette(phase >> 1);
                for (var d = 0; d <= tail; d++) {
                    var idx = (head + span - d) % span;
                    addPixel(idx, c, (tail - d + 1) * 255 / (tail + 1));
                }
                var sparks = Math.trunc(inten / 64);
                for (var s = 0; s < sparks; s++) {
                    var idx = (head + Math.floor(span / 2) + s * 7) % span;
                    setPixel(idx, palette(phase + 96 + s * 53), 120);
                }
                break;
            }
            case 53:
                clear();
                var scale = 12 + Math.trunc(inten / 8);
                var paletteId = node.effectPaletteId ? node.effectPaletteId : 2;
                for (var i = 0; i < leds; i++) {
                    var fromBase = leds - 1 - i;
                    var rise = fromBase * 255 / (leds || 1);
                    var n1 = noise8(i * scale + (phase >> 1), phase >> 2);
                    var n2 = wave8(i * (scale / 2 + 9) - phase);
                    var heat = n1 * 2 / 3 + n2 / 3;
                    heat = heat * (255 - rise / 2) / 255;
                    if (fromBase < leds / 5) heat = heat + (255 - heat) / 3;
                    var v = Math.min(255, heat);
                    setPixel(i, that.effectBuiltinPalette(paletteId, v), v);
                }
                break;
            default:
                clear();
                for (var i = 0; i < leds; i++) setPixel(i, palette(i * 255 / leds + phase), 255);
                break;
        }

        for (var y = 0; y < leds; y++) {
            var src = y * 3;
            var idx = (y * cols + x) * 4;
            image.data[idx] = buffer[src];
            image.data[idx + 1] = buffer[src + 1];
            image.data[idx + 2] = buffer[src + 2];
            image.data[idx + 3] = 255;
        }
    }

    return this.applyEffectTransforms(image, node);
}
