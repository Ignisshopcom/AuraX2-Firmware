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
    this.enable_accelerometer = true;
    this.selected_device = '';
    this.lastSignature = '';
    this.historyEnabled = true;
    this.currentTimeline = '';

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
    this.timelines[hash] = {
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
    this.currentTimeline = hash;
    app_execute_event('project_updated');
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
    this.enable_accelerometer = this.ignis.userconf ? !!this.ignis.userconf.get('last_enable_accelerometer') : true;
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
    this.enable_accelerometer = (project.enable_accelerometer ? project.enable_accelerometer : true);
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
                    if (n.accelerometer) nn.accelerometer = n.accelerometer;
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
                if (n.accelerometer) nn.accelerometer = n.accelerometer;
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
            start: n.start,
            end: n.end,
            duration: n.duration,
            name: path.basename(n.path),
            path: n.path,
            hash: n.hash,
            sep: n.sep,
            dim: n.dim,
            sync: n.sync,
            strobo: n.strobo,
            uid: n.uid,
            gap: n.gap,
            frequency: this.clampLineFrequency(n.frequency),
            picture_frequency: n.picture_frequency,
            accelerometer: n.accelerometer,
            preview_type: n.preview_type,
            mirror: n.mirror,
            rotate: n.rotate,
            reverse: n.reverse,
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
            image_hashes[timeline[i].hash] = timeline[i].name;
        }

        timelines[hash] = {
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
                accelerometer: n.accelerometer,
                preview_type: n.preview_type,
                mirror: n.mirror,
                rotate: n.rotate,
                reverse: n.reverse,
                mgap: n.mgap
            });
        }
        timelines[hash] = {
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
            accelerometer: n.accelerometer,
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
    if (!ln) return;
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
    if (!lib && !n.path) return null;

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
        start: start,
        end: start + duration,
        duration: duration,
        path: n.path || lib.path,
        hash: n.hash,
        sep: (n.sep !== undefined && n.sep !== null) ? n.sep : 80,
        dim: (n.dim !== undefined && n.dim !== null) ? n.dim : 100,
        sync: !!n.sync,
        strobo: !!n.strobo,
        uid: uid,
        tex_loaded: false,
        gap: n.gap || 0,
        frequency: this.clampLineFrequency(n.frequency || this.getDefaultLineFrequency()),
        picture_frequency: n.picture_frequency || 1,
        accelerometer: !!n.accelerometer,
        preview_type: n.preview_type || 0,
        mirror: !!n.mirror,
        rotate: !!n.rotate,
        reverse: !!n.reverse,
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
    this.enable_accelerometer = (project.enable_accelerometer !== undefined && project.enable_accelerometer !== null) ? project.enable_accelerometer : this.enable_accelerometer;
    this.selected_device = project.selected_device || this.selected_device;
    if (project.uniqid && project.uniqid > this.uniqid) this.uniqid = project.uniqid;

    if (project.timelines) {
        var oldCurrentTimeline = this.currentTimeline;
        this.timelines = {};

        for (var hash in project.timelines) {
            var tl = project.timelines[hash];
            this.timelines[hash] = {
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
        if (t.start <= n.end && t.end >= n.start && t.hash == n.hash && t.path == n.path) {
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
                start: n.end + 1,
                end: t.end,
                duration: t.end - n.end + 1,
                path: t.path,
                hash: t.hash,
                sep: t.sep,
                dim: t.dim,
                sync: t.sync,
                strobo: t.strobo,
                gap: t.gap,
                frequency: this.clampLineFrequency(t.frequency),
                picture_frequency: t.picture_frequency,
                accelerometer: t.accelerometer,
                preview_type: t.preview_type,
                mirror: t.mirror,
                rotate: t.rotate,
                reverse: t.reverse,
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
                if (n.mirror) {
                    this.timeline[i].hash = this.getMirrorImageHash(n, this.leds);
                }
            }

            //console.log(data); return;

            var exportTechnology = this.getExportTechnology();
            var exportProcessor = exportTechnology == 'aurax' ? processor_process_aurax : processor_process;

            exportProcessor(this, data, $.proxy(function (result) {

                // vratit hashe zpatky
                for (var i in this.timeline) {
                    var n = this.timeline[i];
                    if (n.mirror) {
                        this.timeline[i].hash = this.timeline[i].hash.split('_')[0];
                    }
                }

                var target = $('#drives').val();

                if (target && target.length > 0) {
                    if (target[target.length-1] != path.sep) target += path.sep;

                    var fn = target + this.getProjectFilename(false, this.getExportExtension());
                    if ($('#filename-editable-btn').hasClass('active')) {
                        fn = target + this.sanitizeExportFilename($('#export-filename').val(), this.getExportExtension());
                    }

                    if (this.saveas) {
                        var tfn = remove_diacritics($('#project-name').val()).toLowerCase();
                        tfn = tfn.replace(/[^a-zA-Z0-9]+/g, ' ').trim().replace(/[ ]+/g, '-');
                        tfn = this.getProjectFilename(true, this.getExportExtension());

                        this.dialog_export_properties.defaultPath = tfn;
                        fn = dialog_save(this.dialog_export_properties);
                    }

                    if (!fn) {
                        app_loading(false);
                        return;
                    }

                    if (fs.existsSync(fn)) {
                        fs.unlinkSync(fn);
                    }

                    fs.writeFile(fn, result, function (e) {
                        app_loading(false);
                        if (e === null) {
                            //alert('Data saved to: ' + fn);
                            this.ignis.properties.updateDeviceFiles();
                        } else {
                            alert('Error saving data! See details in console.');
                            console.log(e);
                        }
                    });
                }
            }, this));
        }
    });
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

IgnisProject.prototype.getExportExtension = function ()
{
    return this.getExportTechnology() == 'aurax' ? 'axp' : 'pix';
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
