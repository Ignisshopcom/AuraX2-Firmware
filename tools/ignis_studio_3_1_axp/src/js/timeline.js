var timeline = {
    canvas: null,
    ctx: null,
    position: 0,
    cursor: 0,
    scale: 100,
    mdown: false,
    hdown: false,
    mx: 0,
    mp: 0,
    hover_index: null,
    hover_mode: 'move',
    hover_offset: 0,
    editor_index: null,
    editor_multi: false,
    cursor_position: 10000,
    selected_items: [],
    clipboard: null
};


function IgnisTimeline(ignis)
{
    this.ignis = ignis;
    this.ignis.timeline = this;

    this.canvas = null;
    this.ctx = null;
    this.position = 0;
    this.cursor = 0;
    this.scale = 100;
    this.real_width = 0;
    this.mdown = false;
    this.hdown = false;
    this.mx = 0;
    this.my = 0;
    this.ox = 0;
    this.oy = 0;
    this.ox = 0;
    this.oe = 0;
    this.hover_index = null;
    this.hover_mode = 'move';
    this.hover_time = 0;
    this.wrapper_time = 0;
    this.hover_offset = 0;
    this.hover_element = null;
    this.editor_index = null;
    this.cache = {};
    this.cursor_position = 0;
    this.selected_nodes = [];
    this.audio_temp = false;
    this.prev_node = null;
    this.prev_hash = null;
    this.prev_nodes = [];
    this.prev_hashes = [];
    this.cursor_helper_start = null;
    this.cursor_helper_end = null;
    this.tex_ex_cache = {};
    this.gmr = null;
    this.gmr_data = {};
    this.mirror_generation = {};
    this.left = 40;
    this.vscroll = 0;
    this.ovs = 0;
    this.switchElements = {};
    this.inactiveTimelinesCache = {};
    this.canVscroll = false;
    this.timelineSizeMultiplier = 30;
    this.timelineSize = 4;
    this.timelineHeight = this.timelineSize * this.timelineSizeMultiplier;
    this.automove = true;
    this.stretch_source = null;
    this.stretch_target_index = null;
    this.selection_box = null;
    this.selection_start = null;
    this.selection_current = null;
    this.selection_additive = false;
    this.nodes_move = null;
    this.suppress_image_context_menu = false;

    for (var i = 0; i < config.preview_instances; i++) {
        this.prev_nodes.push(null);
        this.prev_hashes.push(null);
    }
}

IgnisTimeline.prototype.init = function ()
{
    $('#timeline-canvas').data('timeline', this);
    $('#timeline-panel').data('timeline', this);

    this.canvas = $('#timeline-canvas')[0];
    this.ctx = this.canvas.getContext('2d');

    $(window).on('resize', $.proxy(this.resize, this));
    this.resize();

    this.cursor_helper_start = $('#timeline-helper-cursor-start');
    this.cursor_helper_end = $('#timeline-helper-cursor-end');
    $('#zoom-canvas')[0].width = $('#zoom-canvas').width();
    $('#zoom-canvas')[0].height = $('#zoom-canvas').height();
    this.hideZoom();

    //$('#timeline-canvas').on('drop', $.proxy(this.onDrop, this));
    $('#timeline-images-wrapper').on('dragover', $.proxy(this.onDragOverTimeline, this));
    $('#timeline-timeline').on('drop', $.proxy(this.onDropTimeline, this));
    $('#timeline-timeline').on('mousewheel', $.proxy(this.onMouseWheel, this));
    $('#timeline-timeline').on('mousemove', $.proxy(this.onMouseMove, this));
    $('#timeline-timeline').on('click', $.proxy(this.onClick, this));
    $('#timeline-timeline').on('mousedown', $.proxy(this.onMouseDown, this));
    $('#timeline-timeline').on('contextmenu', function (e) { e.preventDefault(); });
    $('#timeline-cursor-hit').on('mousedown', $.proxy(this.onCursorMouseDown, this));
    $(document).on('mousemove', $.proxy(this.onCursorMouseMove, this));
    $(document).on('mouseup', $.proxy(this.onMouseUp, this));
    $(document).on('mousemove', $.proxy(this.onAudioMouseMove, this));
    $(document).on('keydown', $.proxy(this.onKeyDown, this));
    $(document).on('keydown', $.proxy(this.onGlobalKeyDown, this));
    $(document).on('click', $.proxy(this.hideMenu, this));
    $('[remtl]').on('click', $.proxy(this.removeTimeline, this));
    $('[setprev]').on('click', $.proxy(this.setPreview, this));
    $('[setdmode]').on('click', $.proxy(this.setDefaultPreview, this));
    $('[movetl]').on('click', $.proxy(this.moveTimeline, this));

    app_register_action('audio_play', $.proxy(this.play, this));
    app_register_action('audio_pause', $.proxy(this.pause, this));
    app_register_action('audio_rewind', $.proxy(this.rewind, this));
    app_register_action('audio_forward', $.proxy(this.forward, this));
    app_register_action('audio_beginning', $.proxy(this.rewindFull, this));
    app_register_action('audio_end', $.proxy(this.forwardFull, this));
    app_register_action('audio_automove', $.proxy(this.automove_switch, this));

    app_register_action('zoom_in', $.proxy(this.zoomIn, this));
    app_register_action('zoom_out', $.proxy(this.zoomOut, this));
    app_register_action('zoom_fit', $.proxy(this.zoomFit, this));

    app_register_event('audio_rendered', $.proxy(this.onAudioRendered, this));
    app_register_event('project_updated', $.proxy(this.onProjectUpdated, this));
    app_register_event('project_timelines_updated', $.proxy(this.onProjectTimelinesUpdated, this));

    $('#timeline-switches-scroll').on('mousewheel', $.proxy(this.timelinesScroll, this));
    $('#timeline-height-switch').on('click contextmenu', $.proxy(this.timelineHeightSwitch, this));

    $('#timeline-images-wrapper').on('mousedown', $.proxy(this.imagesWrapperDown, this));
    $('#timeline-images-wrapper').on('mouseup', $.proxy(this.imagesWrapperUp, this));

    this.update(0);
    this.drawTimescale();
}

IgnisTimeline.prototype.imagesWrapperDown = function (e)
{
    this.wrapper_time = Date.now();
}

IgnisTimeline.prototype.imagesWrapperUp = function (e)
{
    if (e.target != document.getElementById('timeline-images-wrapper')) return;
    if (e.ctrlKey) return;
    if (Date.now() - this.wrapper_time < 250) this.deselect();
}

IgnisTimeline.prototype.automove_switch = function (e)
{
    this.automove = !this.automove;
    if (this.automove) {
        $('#automove-button').addClass('active');
    } else {
        $('#automove-button').removeClass('active');
    }
}

IgnisTimeline.prototype.timelineHeightSwitch = function (e)
{
    e.preventDefault();
    var s = this.timelineSize;

    if (e.type == 'click') {
        switch (s) {
            case 4:
                s = 3;
                break;
            case 3:
                s = 2;
                break;
            case 2:
                s = 1;
                break;
            default:
                s = 4;
                break;
        }
    } else {
        switch (s) {
            case 4:
                s = 1;
                break;
            case 3:
                s = 4;
                break;
            case 2:
                s = 3;
                break;
            default:
                s = 2;
                break;
        }
    }

    this.setTimelineHeight(s);
}

IgnisTimeline.prototype.setTimelineHeight = function (s)
{
    $('#timeline-left-panel #timeline-switches .timeline-switch').removeClass('tls-' + this.timelineSize);
    this.timelineSize = s;
    var h = this.timelineHeight = this.timelineSize * this.timelineSizeMultiplier;
    $('#timeline-left-panel #timeline-switches .timeline-switch').css('height', h + 'px');
    $('#timeline-left-panel #timeline-switches .timeline-switch').addClass('tls-' + s);
    $('#timeline-panel #timeline-timeline #timeline-scroll #timelines-inactive .timeline-line').css('height', h + 'px');
    $('#timeline-panel #timeline-timeline #timeline-scroll #timeline-images, #timeline-panel #timeline-timeline #timeline-scroll #timeline-images .timeline-images').css('height', h + 'px');
    this.fixVscroll();
    this.drawTimescale();
    this.onProjectUpdated();
}

IgnisTimeline.prototype.generateMirrorImageExecute = function ()
{
    this.gmr = null;
    this.ignis.resizer.stickMirrorGap(this.gmr_data.hash, this.gmr_data.leds, this.gmr_data.gap, this.gmr_data.rotate, this.gmr_data.reverse, $.proxy(function () {
        this.prev_hash = 'FORCE_REFRESH';
    }, this));
}

IgnisTimeline.prototype.generateMirrorImage = function (hash, leds, gap, rotate, reverse)
{
    this.gmr_data = { hash: hash, leds: leds, gap: gap, rotate: rotate, reverse: !!reverse };
    if (this.gmr != null) {
        clearTimeout(this.gmr);
    }
    this.gmr = setTimeout($.proxy(this.generateMirrorImageExecute, this), 500);
}

IgnisTimeline.prototype.textureExists = function (hash, leds)
{
    var fn = ignis_texdir() + path.sep + hash + '_' + leds + '.jpg';
    if (this.tex_ex_cache[fn]) return true;

    if (fs.existsSync(fn)) {
        this.tex_ex_cache[fn] = true;
        return true;
    }
    return false;
}

IgnisTimeline.prototype.getMirrorFileHash = function (node)
{
    return node.hash + (node.rotate ? '_r' : '_m') + (node.reverse ? 'v' : '') + node.mgap;
}

IgnisTimeline.prototype.getMirrorPreviewPath = function (node, leds)
{
    var fn = ignis_dir('mirror') + path.sep + this.getMirrorFileHash(node) + '_' + leds + '.png';
    return fn.replace(/\\/g, '/');
}

IgnisTimeline.prototype.ensureMirrorPreview = function (node, leds)
{
    var key = this.getMirrorFileHash(node) + '_' + leds;
    var mirrorPath = this.getMirrorPreviewPath(node, leds);
    if (fs.existsSync(mirrorPath) || this.mirror_generation[key]) return;

    this.mirror_generation[key] = true;
    this.ignis.resizer.stickMirrorGap(node.hash, leds, node.mgap, node.rotate, node.reverse, $.proxy(function () {
        delete this.mirror_generation[key];
        this.prev_hash = 'FORCE_REFRESH';
        this.prev_hashes = [];
        this.onProjectUpdated();
        this.updateInactiveTimelines();
        if (this.ignis.preview) this.ignis.preview.nodeChanged();
    }, this));
}

IgnisTimeline.prototype.getNodeDisplayPath = function (node, leds)
{
    if (node.mirror) {
        var mirrorPath = this.getMirrorPreviewPath(node, leds);
        if (fs.existsSync(mirrorPath)) return mirrorPath;
        this.ensureMirrorPreview(node, leds);
    }

    if (this.ignis.library.thumbExist(node.hash)) return this.imageThumbPath(node.hash);
    return 'img/icon.png';
}

IgnisTimeline.prototype.updateNodeElementImage = function (element, node, leds)
{
    if (node.type == 'effect') {
        var colors = node.effectColors || ['#ff6000', '#00b4ff', '#ffffff'];
        var displayKey = 'effect|' + this.ignis.project.effectSignature(node, leds, 256);
        if (element.attr('display-image') == displayKey) return true;
        element.attr('display-image', displayKey);
        element.addClass('timeline-effect');
        if (this.ignis.project && this.ignis.project.effectPreviewDataUrl) {
            element.css('background-image', 'url(' + this.ignis.project.effectPreviewDataUrl(node, 520, 128) + ')');
        } else {
            element.css('background-image', 'linear-gradient(90deg,' + colors.join(',') + ')');
        }
        element.text(node.effectName || node.effectId || 'Effect');
        return true;
    }
    element.removeClass('timeline-effect');
    element.text('');
    var displayPath = this.getNodeDisplayPath(node, leds);
    var displayKey = displayPath + '|' + node.hash + '|' + (node.mirror ? ('m' + node.mgap + '_' + node.rotate + '_' + node.reverse) : 'plain');
    if (element.attr('display-image') == displayKey) return displayPath != 'img/icon.png';

    element.attr('display-image', displayKey);
    if (displayPath == 'img/icon.png') {
        element.css('background-image', 'url(img/icon.png)');
    } else {
        element.css('background-image', 'url(' + displayPath + '?t=' + Date.now() + ')');
    }

    return displayPath != 'img/icon.png';
}

IgnisTimeline.prototype.setPosition = function (i)
{
    if (i < 0) i = 0;
    this.position = i;
    $('#timeline-scroll').css('left', (- this.position / this.scale) + 'px');
    this.onProjectUpdated();
    this.drawTimescale();
}

IgnisTimeline.prototype.update = function (timestamp)
{
    if (this.ignis.audio.started && this.automove) {
        if (this.cursor_position - this.position > this.real_width - Math.round(this.real_width / 20)) {
            this.setPosition(this.position + this.real_width - Math.round(this.real_width / 20) * 2);
        }
        if ( this.cursor_position - this.position < 0 ) {
            this.setPosition(this.cursor_position - Math.round(this.real_width / 20) * 2);
        }
    }

    if (this.selected_nodes && this.selected_nodes.length > 0) {
        this.editor_multi = (this.selected_nodes.length > 1);
    } else if (this.selected_items) {
        this.editor_multi = (this.selected_items.length > 1);
    }

    // update cursor position to audio (if playing)
    if (this.ignis.audio.started) {
        var pos = this.ignis.audio.getPosition();
        this.cursor_position = pos * 1000;
    }

    for (var hash in this.ignis.project.timelines) {
        var tl = this.ignis.project.timelines[hash];
        if (tl.preview === false) continue;
        var pid = tl.preview;
        var current_node = this.getNodeAtEx(hash, this.cursor_position);
        if (current_node) {
            var current_hash = current_node.hash;
            var file_hash = current_node.hash + (current_node.mirror ? (current_node.rotate ? '_r' : '_m') + (current_node.reverse ? 'v' : '') + current_node.mgap : '');
            var texturePath = null;
            var ratio = 1;
            if (current_node.type == 'effect') {
                var effectTexture = this.ignis.project.ensureEffectTexture(current_node, tl.leds);
                current_hash = effectTexture.signature;
                file_hash = 'effect_' + effectTexture.signature;
                texturePath = effectTexture.path + '?t=' + Date.now();
                ratio = effectTexture.ratio;
            }
            if (current_node != this.prev_nodes[pid] || file_hash != this.prev_hashes[pid]) {
                if (current_node.type != 'effect') ratio = this.ignis.library.getImageByHash(current_hash).resolution.r;
                this.prev_nodes[pid] = current_node;
                this.prev_hashes[pid] = file_hash;
                var t = Math.floor((new Date()).getTime() / 1000);
                if (current_node.type != 'effect' && current_node.mirror && !this.textureExists(file_hash, tl.leds)) {
                    this.generateMirrorImage(current_hash, tl.leds, current_node.mgap, current_node.rotate, current_node.reverse);
                }
                this.ignis.preview.setTextureIdx(pid, texturePath || (ignis_texdir() + '/' + file_hash + '_' + tl.leds + '.jpg?t='+t), current_node, ratio);
            }
            this.ignis.preview.syncTimelinePositionIdx(pid, current_node, this.cursor_position, timestamp, this.ignis.audio.started);
            this.ignis.preview.running[pid] = true;
        } else {
            this.ignis.preview.running[pid] = false;
            this.prev_nodes[pid] = null;
        }
    }

    /*var current_node = this.getNodeAt(this.cursor_position);
    if (current_node) {
        var current_hash = current_node.hash;
        var file_hash = current_node.hash + (current_node.mirror ? (current_node.rotate ? '_r' : '_m') + (current_node.reverse ? 'v' : '') + current_node.mgap : '');
        //if (current_hash != this.crhsh) {
        if (current_node != this.prev_node || file_hash != this.prev_hash) {
            var ratio = this.ignis.library.getImageByHash(current_hash).resolution.r;
            this.crhsh = current_hash;
            this.prev_node = current_node;
            this.prev_hash = file_hash;
            var t = Math.floor((new Date()).getTime() / 1000);
            if (current_node.mirror && !this.textureExists(file_hash, this.ignis.project.leds)) {
                this.generateMirrorImage(current_hash, this.ignis.project.leds, current_node.mgap, current_node.rotate, current_node.reverse);
            }
            this.ignis.preview.setTextureIdx(0, ignis_texdir() + '/' + file_hash + '_' + this.ignis.project.leds + '.jpg?t='+t, current_node, ratio);
            this.ignis.preview.running[0] = true;
        }
    } else {
        this.ignis.preview.running[0] = false;
        this.crhsh = null;
        this.prev_node = null;
    }*/

    for (var hash in this.ignis.project.timelines) {
        var current_node = this.getNodeAtEx(hash, this.cursor_position);
        if (current_node) {
            this.ignis.preview.multi_nodes[hash] = current_node;
            if (current_node.type == 'effect') {
                this.ignis.preview.multi_ratios[hash] = this.ignis.project.ensureEffectTexture(current_node, this.ignis.project.timelines[hash].leds).ratio;
            } else {
                this.ignis.preview.multi_ratios[hash] = this.ignis.library.getImageByHash(current_node.hash).resolution.r;
            }
            this.ignis.preview.syncMultiTimelinePosition(hash, current_node, this.cursor_position, timestamp, this.ignis.audio.started);
        } else {
            this.ignis.preview.multi_nodes[hash] = false;
        }
    }

    this.ignis.preview.animate(timestamp);

    $('#timeline-cursor').css('left', Math.round(this.cursor_position / this.scale) + 'px');

    var time = Math.floor(this.cursor_position);
    var ms = ('' + (time % 1000)).padStart(3, '0');
    var s = ('' + Math.floor((time % 60000) / 1000)).padStart(2, '0');
    var m = Math.floor(time / 60000);

    $('#cursor-time-time').html(m + ':' + s + '.' + ms);

    window.requestAnimationFrame($.proxy(this.update, this));
    
    /*
    //if (!this.ignis.preview.running)
    this.redraw(true);
    this.ignis.preview.animate(timestamp);
    */
}

IgnisTimeline.prototype.timelinesScroll = function (e)
{
    this.vscroll -= e.originalEvent.wheelDelta / 6;
    this.fixVscroll();
    this.drawTimescale();
    this.onProjectUpdated();
}

IgnisTimeline.prototype.hideMenu = function (e)
{
    if (!$("#timeline-switch-menu").is(':visible')) return;

    var t = $(e.target);

    while (t.length > 0) {
        var id = t.attr('id');
        if (id == 'timeline-switch-menu') return;
        if (t.hasClass('tsm')) return;
        t = t.parent();
    }
    
    $("#timeline-switch-menu").hide();
}

IgnisTimeline.prototype.onProjectTimelinesUpdated = function ()
{
    this.switchElements = {};
    $('#timeline-switches-scroll').empty();
    var ntb = $('<div id="new-timeline-btn"><div class="number">+</div></div>');
    ntb.on('click', $.proxy(function () {
        this.switchTimeline(this.ignis.project.addTimeline());
    }, this));
    $('#timeline-switches-scroll').append(ntb);
    this.inactiveTimelinesCache = {};
    $('#timelines-inactive').empty();
    this.updateInactiveTimelines();
    this.updateTimelineSwitches();
    this.drawTimescale();
    this.clearPreviews();
}

IgnisTimeline.prototype.clipboardCopy = function ()
{
    this.clipboard = [];
    var nodes = this.getSelectedNodes();
    for (var selected of nodes) {
        var timeline = this.ignis.project.timelines[selected.timelineHash];
        if (!timeline) continue;
        var node = timeline.data[selected.index];
        if (!node) continue;
        var item = {};

        for (var nk in node) {
            if (nk == 'el') continue;
            item[nk] = node[nk];
        }
        item.timelineHash = selected.timelineHash;

        this.clipboard.push(item);
    }

    if (this.clipboard.length > 0) {
        $('#clipbard-status').show();
        $('#clipbard-status').find('.count').text(this.clipboard.length);
    } else {
        $('#clipbard-status').hide();
    }
}

IgnisTimeline.prototype.clipboardPaste = function (fixed)
{
    if (!this.clipboard || this.clipboard.length == 0) return;
    
    var toffset = -1;
    for (var n of this.clipboard) {
        if (n.start < toffset || toffset < 0) {
            toffset = n.start;
        }
    }

    var clipboardTimelineHashes = [];
    for (var n of this.clipboard) {
        if (n.timelineHash && clipboardTimelineHashes.indexOf(n.timelineHash) < 0) {
            clipboardTimelineHashes.push(n.timelineHash);
        }
    }
    var preserveTimelines = clipboardTimelineHashes.length > 1;
    var originalTimeline = this.ignis.project.currentTimeline;
    for (var n of this.clipboard) {
        var start = (fixed ? n.start : (n.start - toffset) + this.cursor_position);
        var targetTimeline = (preserveTimelines && n.timelineHash && this.ignis.project.timelines[n.timelineHash]) ? n.timelineHash : originalTimeline;
        this.ignis.project.switchTimeline(targetTimeline);
        var nn = this.ignis.project.addNode(n.hash, n.path, start, n.duration, false);
        for (var k in n) {
            if (k == 'start' || k == 'end' || k == 'duration' || k == 'uid' || k == 'tex_loaded' || k == 'timelineHash') continue;
            nn[k] = n[k];
        }
    }
    this.ignis.project.switchTimeline(originalTimeline);
    this.updateInactiveTimelines();
    this.onProjectUpdated();
}

IgnisTimeline.prototype.moveTimelineUp = function (hash)
{
    var prev = false;
    for (var i in this.ignis.project.timelines) {
        if (i == hash) break;
        prev = i;
    }
    if (prev) {
        this.ignis.project.swapTimelines(hash, prev);
        this.onProjectTimelinesUpdated();
    }
}

IgnisTimeline.prototype.moveTimelineDown = function (hash)
{
    var next = false;
    var go = false;
    for (var i in this.ignis.project.timelines) {
        if (i == hash) {
            go = true;
            continue;
        }
        if (go) {
            next = i;
            break;
        }
    }
    if (next) {
        this.ignis.project.swapTimelines(hash, next);
        this.onProjectTimelinesUpdated();
    }
}

IgnisTimeline.prototype.onKeyDown = function (e)
{
    if (e.originalEvent.ctrlKey) {
        if (e.keyCode == 67) {
            // copy
            this.clipboardCopy();
        }
        if (e.keyCode == 86) {
            // paste
            this.clipboardPaste(e.shiftKey);
        }
    }
    if (e.keyCode == 33) {
        // up
        this.moveTimelineUp(this.ignis.project.currentTimeline);
    }
    if (e.keyCode == 34) {
        // down
        this.moveTimelineDown(this.ignis.project.currentTimeline);
    }

    var as = this.ignis.audio.started;
    if (e.keyCode == 32 || e.keyCode == 179) {
        if (as) {
            this.pause();
        } else {
            this.play();
        }
    }
    if (e.keyCode == 37) {
        if (as) this.pause();
        this.cursor_position -= 1000;
        if (this.cursor_position < 0) this.cursor_position = 0;
        if (as) this.play();
    }
    if (e.keyCode == 39) {
        if (as) this.pause();
        this.cursor_position += 1000;
        if (as) this.play();
    }
    if (e.keyCode == 46) {
        this.removeSelected();
    }
}

IgnisTimeline.prototype.removeSelected = function ()
{
    var nodes = this.getSelectedNodes();
    if (!nodes || nodes.length == 0) return;

    $('.delete-popup').remove();

    var project = this.ignis.project;
    var originalTimeline = project.currentTimeline;
    var byTimeline = {};

    for (var i in nodes) {
        var selected = nodes[i];
        if (!project.timelines[selected.timelineHash]) continue;
        if (!byTimeline[selected.timelineHash]) byTimeline[selected.timelineHash] = {};
        byTimeline[selected.timelineHash][selected.uid] = true;
    }

    project.historyPush();

    for (var hash in byTimeline) {
        if (!project.timelines[hash]) continue;
        project.switchTimeline(hash);

        for (var i = 0; i < project.timeline.length; i++) {
            var node = project.timeline[i];
            if (node && byTimeline[hash][node.uid]) {
                project.timeline[i] = null;
            }
        }

        project.recalculate();
    }

    if (project.timelines[originalTimeline]) {
        project.switchTimeline(originalTimeline);
    }

    this.clearSelectionVisuals();
    this.updateInactiveTimelines();
    this.onProjectUpdated();
}

IgnisTimeline.prototype.onCursorMouseDown = function (e)
{
    if (!this.isTimescaleHit(e)) return;

    e.preventDefault();
    e.stopPropagation();
    this.cursor_mdown = true;
    this.cursor_mdown_as = this.ignis.audio.started;
    this.cursor_position = this.position + (e.pageX - this.left) * this.scale;
    if (this.cursor_position < 0) this.cursor_position = 0;
    this.showZoom();
    this.updateZoom(this.cursor_position);
    if (this.ignis.audio.started) this.pause();
}

IgnisTimeline.prototype.onCursorMouseMove = function (e)
{
    this.realMouseX = (e.pageX - this.left);
    if (!this.cursor_mdown) return;
    this.cursor_position = this.position + (e.pageX - this.left) * this.scale;
    if (this.cursor_position < 0) this.cursor_position = 0;
    this.updateZoom(this.cursor_position);
}

IgnisTimeline.prototype.isTimescaleHit = function (e)
{
    var offset = $('#timeline-timeline').offset();
    if (!offset) return false;
    var y = e.pageY - offset.top;
    return y >= 0 && y <= 15;
}

IgnisTimeline.prototype.onMouseDown = function (e)
{
    this.ignis.properties.deselect();
    if (e.button == 0 && this.isTimescaleHit(e)) {
        if (!e.ctrlKey) this.clearSelectionVisuals();
        this.onCursorMouseDown(e);
        return;
    }

    if (e.button == 2 && !this.ignis.library.selected_item && $(e.target).closest('#timeline-audio, #timeline-cursor').length == 0) {
        this.suppress_image_context_menu = ($(e.target).closest('.timeline-img, .img-inactive').length > 0);
        var targetIndex = this.getTimelineIndexAtPageY(e.pageY);
        var hashes = this.getTimelineHashes();
        if (hashes[targetIndex]) this.switchTimeline(hashes[targetIndex]);

        if (this.cursor_mdown) return;
        this.mdown = true;
        this.mx = (e.pageX - this.left);
        this.my = e.pageY;
        this.hover_mode = 'scroll';
        this.ovs = this.vscroll;
        this.oe = this.position;
        this.canVscroll = false;
        e.preventDefault();
        return;
    }

    if (e.button == 0 && !this.ignis.library.selected_item && this.isTimelineSelectionSurface(e.target)) {
        if (this.cursor_mdown) return;
        this.mdown = true;
        this.mx = (e.pageX - this.left);
        this.my = e.pageY;
        this.hover_mode = 'selection_box';
        this.selection_additive = e.ctrlKey;
        this.selection_start = this.getSelectionPoint(e);
        this.selection_current = this.selection_start;
        if (!this.selection_additive) {
            this.clearSelectionVisuals();
        }
        this.ensureSelectionBox();
        this.updateSelectionBox(e);
        e.preventDefault();
        return;
    }

    if (!$(e.target).hasClass('active') && !$(e.target).hasClass('selected')) {
        if (e.button == 2 || e.button == 0) {
            var id = ($(e.target).attr('id') || $(e.target).parent().parent().parent().attr('id'))
            if (id == 'timeline-images-wrapper') {
                var i = e.pageY - $('#timeline-images-wrapper').offset().top + this.vscroll;
                i = Math.floor(i / this.timelineHeight);
                var c = 0;
                for (var hash in this.ignis.project.timelines) {
                    if (c == i) {
                        this.switchTimeline(hash);
                        break;
                    }
                    c++;
                }
            }
        }
        
        if (!this.ignis.library.selected_item && e.button == 2) {
            if (this.cursor_mdown) return;
            this.mdown = true;
            this.mx = (e.pageX - this.left);
            this.my = e.pageY;
            this.hover_mode = 'scroll';
            this.ovs = this.vscroll;
            this.oe = this.position;
            this.canVscroll = false;
            e.preventDefault();
        } else if (this.ignis.library.selected_item && e.button == 0) {
            this.mdown = true;
            this.mx = (e.pageX - this.left);
            this.my = e.pageY;
            this.hover_mode = 'create';
            var now = Date.now();
            var dt = now - this.hover_time;
            if (dt < 500) {
                this.hover_mode = 'move';
                this.mdown = false;
                this.ignis.library.deselect();
                return;
            }
            this.hover_time = Date.now();
            this.oe = this.position;
            $('.timeline-img').removeClass('active');
            
            var n = this.ignis.library.selected_item;
            //this.ignis.library.clearSelected();
            var time = Math.floor(this.position + (e.originalEvent.pageX - this.left) * this.scale);
            this.create_node = n;

            var s = (e.pageX - this.left);
            s = s + this.position / this.scale;

            this.create_element = $('<div></div>');
            this.create_element.addClass('img');
            this.create_element.addClass('timeline-img');
            this.create_element.addClass('timeline-img-create');
            this.create_element.css('left', s + 'px');

            if (n.type == 'effect') {
                this.create_element.addClass('timeline-effect');
                this.create_element.css('background-image', 'url(' + this.ignis.project.effectPreviewDataUrl(n, 520, 128) + ')');
                this.create_element.text(n.name || 'Effect');
            } else if (this.ignis.library.thumbExist(n.hash)) {
                var t = Math.floor((new Date()).getTime() / 1000);
                this.create_element.css('background-image', 'url(' + this.imageThumbPath(n.hash) + '?t='+t+')');
            } else {
                this.create_element.css('background-image', 'url(img/icon.png)');
            }

            this.create_element.css('width', '0px');
            this.create_element.css('bacgkround-color', '#FF0000');

            $('#timeline-images').append(this.create_element);

            this.hover_mode = 'create';
        }
    }
}

IgnisTimeline.prototype.onClick = function (e)
{
    if (this.mx != (e.pageX - this.left)) return;

    $('.audio-scroller').removeClass('selected');

    if (e.offsetY < 15) {
        var as = this.ignis.audio.started;
        if (as) this.pause();
        this.cursor_position = this.position + (e.pageX - this.left) * this.scale;
        if (as) this.play();
    }

    /*this.cursor_position = this.position + (e.pageX - this.left) * this.scale;
    if (this.ignis.audio.started) {
        this.ignis.audio.play(this.cursor_position);
    }*/
}

IgnisTimeline.prototype.onMouseUp = function (e)
{
    this.cursor_helper_start.hide();
    this.cursor_helper_end.hide();
    this.hideZoom();

    if (this.audio_move) {
        var d = (e.pageX - this.left) - this.aduio_move_x;
        this.ignis.project.audio_offset = this.aduio_move_offset + d * this.scale;
        this.audio_move = false;
    }
    if (this.cursor_mdown) {
        this.cursor_mdown = false;
        this.hideZoom();
        this.cursor_position = this.position + (e.pageX - this.left) * this.scale;
        if (this.cursor_position < 0) this.cursor_position = 0;
        if (this.cursor_mdown_as) {
            this.play();
        }
    }
    if (!this.mdown) return;
    const project = this.ignis.project;

    var dx = (e.pageX - this.left) - this.mx;
    this.mdown = false;

    if (this.hover_element) {
        this.hover_index = this.hover_element.attr('idx');
    }

    if (this.hover_mode == 'selection_box') {
        var sx = this.selection_start ? this.selection_start.pageX : e.pageX;
        var sy = this.selection_start ? this.selection_start.pageY : e.pageY;
        var moved = Math.abs(e.pageX - sx) > 3 || Math.abs(e.pageY - sy) > 3;
        this.updateSelectionBox(e);
        if (moved) {
            this.selectNodesInSelection(this.selection_additive);
        } else if (!this.selection_additive) {
            var targetIndex = this.getTimelineIndexAtPageY(e.pageY);
            var hashes = this.getTimelineHashes();
            if (hashes[targetIndex]) this.switchTimeline(hashes[targetIndex]);
        }
        this.removeSelectionBox();
        this.hover_index = null;
        this.hover_element = null;
        this.hover_mode = 'none';
        return;
    }

    if (this.hover_mode == 'stretch_timelines') {
        var targetIndex = this.getTimelineIndexAtPageY(e.pageY);
        this.finishStretchTimelineImage(targetIndex);
        this.hover_index = null;
        this.hover_element = null;
        this.hover_mode = 'none';
        return;
    }

    if (this.hover_mode == 'nodes_move') {
        var delta = this.getNodesMoveDelta(dx);
        this.finishSelectedNodesMove(delta);
        this.hover_index = null;
        this.hover_element = null;
        this.hover_mode = 'none';
        return;
    }

    if (this.hover_mode == 'move') {
        project.historyPush();
        project.timeline[this.hover_index].start += dx * this.scale;
        if (project.timeline[this.hover_index].start < 0) project.timeline[this.hover_index].start = 0;
        project.timeline[this.hover_index].end = project.timeline[this.hover_index].start + project.timeline[this.hover_index].duration;
        project.recalculate(this.hover_index);
    }

    if (this.hover_mode == 'multi_move') {
        for (var i of this.selected_items) {
            var x = this.multi_positions[i] + ((e.pageX - this.left) - this.mx);
            $('.timeline-img[idx='+i+']').css('left', x + 'px');
            project.timeline[i].start += dx * this.scale;
            if (project.timeline[i].start < 0) project.timeline[i].start = 0;
            project.timeline[i].end = project.timeline[i].start + project.timeline[i].duration;
        }
        project.recalculate();
        project.recalculate();
    }

    if (this.hover_mode == 'right') {
        project.historyPush();
        project.timeline[this.hover_index].duration = this.hover_element.width() * this.scale;
        project.timeline[this.hover_index].end = project.timeline[this.hover_index].start + project.timeline[this.hover_index].duration;
        project.recalculate(this.hover_index);
    }

    if (this.hover_mode == 'left') {
        project.historyPush();
        project.timeline[this.hover_index].start += dx * this.scale;
        if (project.timeline[this.hover_index].start < 0) project.timeline[this.hover_index].start = 0;
        project.timeline[this.hover_index].duration = project.timeline[this.hover_index].end - project.timeline[this.hover_index].start;
        project.recalculate(this.hover_index);
    }

    if (this.hover_mode == 'create') {
        project.historyPush();
        var s = this.mx;
        var e = (e.pageX - this.left);
        if (e < s) {
            var t = s;
            s = e;
            e = t;
        }
        if (s + this.position / this.scale < 0) s = -this.position / this.scale;
        if (e < s) e = s;
        var w = e - s;
        var time = this.position + s * this.scale;
        if (time < 0) time = 0;
        var duration = w * this.scale;
        if (duration == 0) {
            /*this.deselect();
            var n = this.ignis.project.getNodeAt(time);
            if (n) {
                this.selectNode(n.uid);
            }*/
            this.ignis.library.deselect();
            this.create_element.remove();
            return;
        }

        var lib = this.create_node;
        project.addNode(lib.hash, lib.path, time, duration, true);
        this.create_element.remove();
        this.selectLatestNode();
    }
    
    this.hover_index = null;
    this.hover_element = null;
    this.hover_mode = 'none';

    if (this.editor_index !== null) this.ignis.properties.editorSet(this.editor_index);
}

IgnisTimeline.prototype.selectLatestNode = function ()
{
    var idx = this.ignis.project.timeline.length - 1;
    this.editor_index = idx;
    this.selected_items = [ idx ];
    this.selected_nodes = [{
        timelineHash: this.ignis.project.currentTimeline,
        uid: this.ignis.project.timeline[idx].uid,
        index: idx,
    }];
    this.ignis.properties.editorSet(this.editor_index);
    $('.timeline-img').removeClass('selected');
    $('.timeline-img').removeClass('active');
    $('.timeline-img[idx='+idx+']').addClass('selected');
    $('.timeline-img[idx='+idx+']').addClass('active');
}

IgnisTimeline.prototype.onMouseMove = function (e)
{
    var dx = (e.pageX - this.left) - this.mx;
    var dy = e.pageY - this.my;

    if (this.mdown) {
        if (this.hover_mode == 'selection_box') {
            this.updateSelectionBox(e);
            return;
        }

        if (this.hover_mode == 'nodes_move') {
            this.previewSelectedNodesMove(this.getNodesMoveDelta(dx));
            return;
        }

        if (this.hover_mode == 'move') {
            var x = (this.position / this.scale) + (e.pageX - this.left) - this.ox;
            if (x < 0) x = 0;
            this.hover_element.css('left', x + 'px');
            this.cursor_helper_start.css('left', x + 'px');
            this.cursor_helper_end.css('left', (this.hover_element.width() + x) + 'px');
        }

        if (this.hover_mode == 'multi_move') {
            for (var i of this.selected_items) {
                var x = (this.position / this.scale) + this.multi_positions[i] + ((e.pageX - this.left) - this.mx);
                $('.timeline-img[idx='+i+']').css('left', (x - 40) + 'px');
            }
        }

        if (this.hover_mode == 'right') {
            var w = (this.position / this.scale) + (e.pageX - this.left) - this.hover_element.position().left + (this.ow - this.ox);
            this.hover_element.css('width', w + 'px');
            this.cursor_helper_end.css('left', (this.hover_element.position().left + w) + 'px');

            var p = Math.round(this.position + ((e.pageX - this.left)) * this.scale);
            this.updateZoom(p - 1, (e.pageX - this.left) + (this.ow - this.ox));
        }

        if (this.hover_mode == 'left') {
            var x = (this.position / this.scale) + (e.pageX - this.left) - this.ox;
            if (x < 0) x = 0;
            var w = this.oe - x;
            this.hover_element.css('left', x + 'px');
            this.hover_element.css('width', w + 'px');
            this.cursor_helper_start.css('left', x + 'px');

            var p = Math.round(this.position + ((e.pageX - this.left) - this.ox) * this.scale);
            this.updateZoom(p, (e.pageX - this.left) - this.ox);
        }

        if (this.hover_mode == 'scroll') {
            this.position = this.oe - (dx * this.scale);
            if (this.position < 0) this.position = 0;
            $('#timeline-scroll').css('left', (- this.position / this.scale) + 'px');
            this.onProjectUpdated();
            this.drawTimescale();

            if (this.canVscroll) {
                this.vscroll = this.ovs - dy;
                this.fixVscroll();
            } else {
                if (Math.abs(dy) > 16) this.canVscroll = true;
            }
        }

        if (this.hover_mode == 'create') {
            var s = this.mx;
            var e = (e.pageX - this.left);
            if (e < s) {
                var t = s;
                s = e;
                e = t;
            }
            if (s + this.position / this.scale < 0) s = -this.position / this.scale;
            if (e < s) e = s;
            var w = e - s;

            s = s + this.position / this.scale;

            this.create_element.css('left', s+'px');
            this.create_element.css('width', w+'px');
        }

        if (this.hover_mode == 'stretch_timelines') {
            this.updateStretchTimelinePreview(this.getTimelineIndexAtPageY(e.pageY));
        }
    }

    if (this.editor_index !== null) this.ignis.properties.editorSet(this.editor_index);
}

IgnisTimeline.prototype.fixVscroll = function()
{
    var maxvscroll = (Object.keys(this.ignis.project.timelines).length - 1) * this.timelineHeight;
    var tth = maxvscroll + this.timelineHeight;
    var tiwh = $('#timeline-images-wrapper').height();

    if (tth - this.vscroll < tiwh) this.vscroll = tth - tiwh;

    if (tiwh > tth) this.vscroll = 0;
    if (this.vscroll > maxvscroll) this.vscroll = maxvscroll;
    if (this.vscroll < 0) this.vscroll = 0;
    $('#timeline-images').css('top', (this.getImagesTop()) + 'px');
    $('#timeline-switches-scroll').css('top', (0 - this.vscroll) + 'px');
}

IgnisTimeline.prototype.timelineSwitchMenuAction = function (e)
{
    var hash = $(this).attr('hash');
    var tl = ignis.project.timelines[hash];
    $('.tsm-mode').removeClass('active');
    $('.tsm-item').removeClass('active');
    $('.tsm-mode[setdmode='+tl.default_mode+']').addClass('active');
    if (tl.preview !== false && tl.preview !== null) {
        $('.tsm-item[setprev='+tl.preview+']').addClass('active');
    }

    var t = e.pageY - $("#timeline-switch-menu").height();
    $("#timeline-switch-menu").css('top', t + 'px');
    $("#timeline-switch-menu").attr('hash', hash);
    $("#timeline-switch-menu").show();
}

IgnisTimeline.prototype.updateTimelineSwitches = function ()
{
    //timeline-switch
    var project = this.ignis.project;

    //$('#timeline-switches-scroll').empty();
    var c = 1;
    for (var i in project.timelines) {
        var tl = project.timelines[i];
        if (!this.switchElements[i]) {
            var el = $('<div></div>');
            var n = $('<div class="number tsm"></div>');
            n.text(c++);
            n.attr('hash', i);
            n.on('click', this.timelineSwitchMenuAction);
            el.append(n);
            el.addClass('timeline-switch');
            el.addClass('tls-' + this.timelineSize);
            el.css('height', this.timelineHeight + 'px');
            var p = $('<div class="preview"></div>');
            if (tl.preview !== false && tl.preview !== null && tl.preview >= 0) {
                p.append($('<i class="far fa-eye"></i>'));
                p.append(document.createTextNode(tl.preview + 1));
            } else {
                p.append($('<i class="far fa-eye-slash"></i>'));
            }
            el.append(p);
            var menu = $('<div class="menu tsm"><i class="fas fa-ellipsis-h"></i></div>');
            menu.attr('hash', i);
            menu.on('click', this.timelineSwitchMenuAction);
            el.append(menu);
            el.attr('hash', i);
            el.on('click', $.proxy(this.onTimelineSwitchClick, this));
            el.on('contextmenu', this.timelineSwitchMenuAction);
            if (project.currentTimeline == i) {
                el.addClass('active');
            }
            el.insertBefore('#new-timeline-btn');
            //$('#timeline-switches-scroll').append(el);
            this.switchElements[i] = el;
        }
    }

    var c = 1;
    $('.timeline-switch').each(function () {
        $(this).find('.number').text(c++);
    });

    this.updateImagesTop();

    this.ignis.preview.updateMultiPreview();
}

IgnisTimeline.prototype.getImagesTop = function ()
{
    var project = this.ignis.project;
    var c = 0;
    for (var i in project.timelines) {
        if (i == project.currentTimeline) {
            return (c * this.timelineHeight - this.vscroll);
        }
        c++;
    }
    return 0;
}

IgnisTimeline.prototype.removeTimeline = function (e)
{
    this.ignis.project.removeTimeline(this.ignis.project.currentTimeline);
    $('#timeline-switch-menu').hide();

    for (var hash in this.ignis.project.timelines) {
        this.switchTimeline(hash);
        return;
    }
}

IgnisTimeline.prototype.moveTimeline = function (e)
{
    $("#timeline-switch-menu").hide();
    var hash = $("#timeline-switch-menu").attr('hash');

    var dir = $(e.target).attr('movetl');
    if (!dir) dir = $(e.target).parent().attr('movetl');

    if (dir == 'up') {
        this.moveTimelineUp(hash);
    } else {
        this.moveTimelineDown(hash);
    }
}

IgnisTimeline.prototype.setDefaultPreview = function (e)
{
    const project = this.ignis.project;

    $('#timeline-switch-menu').hide();
    var i = parseInt( $(e.target).attr('setdmode') );
    if (isNaN(i)) {
        var i = parseInt( $(e.target).parent().attr('setdmode') );
    }

    project.setCurrentDefaultMode(i);
}

IgnisTimeline.prototype.setPreview = function (e)
{
    const project = this.ignis.project;

    $('#timeline-switch-menu').hide();
    var i = parseInt( $(e.target).attr('setprev') );

    project.setCurrentPreview(i);

    this.clearPreviews();
}

IgnisTimeline.prototype.clearPreviews = function ()
{
    const project = this.ignis.project;
    
    var previews = [];
    for (var hash in project.timelines) {
        var tl = project.timelines[hash];

        if (tl.preview) previews.push(tl.preview);
    }

    for (var p = 0; p < this.ignis.preview.instances; p++) {
        if (!previews.includes(p)) this.ignis.preview.running[p] = false;
    }
}

IgnisTimeline.prototype.resetTimelineDom = function ()
{
    $('.delete-popup').remove();
    $('.timeline-img').remove();
    $('#timelines-inactive').empty();
    this.inactiveTimelinesCache = {};
    this.clearSelectionVisuals();
    this.nodes_move = null;
    this.hover_index = null;
    this.hover_element = null;
    this.hover_mode = 'none';
}

IgnisTimeline.prototype.updateInactiveTimelines = function ()
{
    // #timeline-inactive
    var project = this.ignis.project;
    var el = $('#timelines-inactive');

    for (var cachedHash in this.inactiveTimelinesCache) {
        if (!project.timelines[cachedHash]) {
            this.inactiveTimelinesCache[cachedHash].line.remove();
            delete this.inactiveTimelinesCache[cachedHash];
        }
    }

    var c = 0;
    for (var hash in project.timelines) {
        var tl = project.timelines[hash].data;
        if (!this.inactiveTimelinesCache[hash]) {
            this.inactiveTimelinesCache[hash] = {
                line: $('<div></div>'),
                imgs: {},
            };
            this.inactiveTimelinesCache[hash].line.addClass('timeline-line');
            this.inactiveTimelinesCache[hash].line.css('height', this.timelineHeight + 'px');
            this.inactiveTimelinesCache[hash].line.attr('hash', hash);
            this.inactiveTimelinesCache[hash].line.css('top', (c * this.timelineHeight) + 'px');
            el.append(this.inactiveTimelinesCache[hash].line);
            c++;
        } else {
            this.inactiveTimelinesCache[hash].line.css('top', (c * this.timelineHeight) + 'px');
            c++;
        }
        var cache = this.inactiveTimelinesCache[hash];
        for (var n of tl) {
            if (!n) continue;
            var ihash = n.uid;
            if (!cache.imgs[ihash]) {
                var img = $('<div></div>');
                img.addClass('img');
                img.addClass('img-inactive');
                img.attr('hash', ihash);
                img.attr('timeline-hash', hash);
                img.attr('uid', n.uid);
                img.on('click', $.proxy(this.onInactiveImageClick, this));
                img.on('mousedown', $.proxy(this.onInactiveImageMouseDown, this));
                img.on('contextmenu', function (e) { e.preventDefault(); });
                cache.line.append(img);
                cache.imgs[ihash] = img;
            }
            cache.imgs[ihash].attr('timeline-hash', hash);
            cache.imgs[ihash].attr('uid', n.uid);
            this.updateNodeElementImage(cache.imgs[ihash], n, project.timelines[hash].leds);
            this.applyInactiveSelectionState(cache.imgs[ihash], hash, n.uid);
            
            cache.imgs[ihash].css('left', Math.round(n.start / this.scale) + 'px');
            cache.imgs[ihash].css('width', Math.round(n.duration / this.scale) + 'px');
        }

        for (var ihash in cache.imgs) {
            var skip = false;
            for (var n of tl) {
                if (!n) continue;
                if (n.uid == ihash) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
            cache.imgs[ihash].remove();
            delete cache.imgs[ihash];
        }
    }
}

IgnisTimeline.prototype.updateImagesTop = function ()
{
    $('#timeline-images').css('top', this.getImagesTop() + 'px');
    $('#timelines-inactive').css('top', (-this.vscroll) + 'px');
    this.updateInactiveTimelines();
}

IgnisTimeline.prototype.onTimelineSwitchClick = function (e)
{
    var el = $(e.currentTarget);
    var hash = el.attr('hash');
    this.switchTimeline(hash);
}

IgnisTimeline.prototype.switchTimeline = function (hash)
{
    this.timelineSwitchTime = Date.now();
    $('.timeline-switch').removeClass('active');
    $('.timeline-switch[hash='+hash+']').addClass('active');
    this.ignis.project.switchTimeline(hash);
    this.syncCurrentTimelineSelection();
    this.refreshSelectionVisuals();
    this.updateImagesTop();
}

IgnisTimeline.prototype.onAudioRendered = function ()
{
    $('#timeline-audio').find('.part').remove();
    //$('#timeline-audio').empty();

    var x = this.ignis.project.audio_offset / this.scale;
    var ox = x;
    var tw = 0;
    for (var i in this.ignis.audio.envelopeBuffers) {
        var img = this.ignis.audio.envelopeBuffers[i];
        var w = Math.round(img.width / this.scale);
        $(img).css('width', w + 'px');
        $(img).css('height', '70px');
        var el = $('<div class="part"></div>');
        el.css('left', x + 'px');
        el.css('width', w + 'px');
        el.css('height', '70px');
        el.append(img);
        $('#timeline-audio').prepend(el);
        x += w;
        tw += w;
    }

    var audio_scroller = $('.audio-scroller');
    if (audio_scroller.length == 0) {
        audio_scroller = $('<div class="audio-scroller"></div>');
        $('#timeline-audio').append(audio_scroller);
        audio_scroller.on('mousedown', $.proxy(this.onAudioMouseDown, this));        
    }
    audio_scroller.css('left', ox + 'px');
    audio_scroller.css('width', tw + 'px');
    audio_scroller.on('click', $.proxy(function (e) {
        e.stopPropagation();
        $(e.target).addClass('selected');
    }, this));
}

IgnisTimeline.prototype.onAudioMouseDown = function (e)
{
    if (!$(e.target).hasClass('selected')) return;
    e.stopPropagation();
    this.pause();
    this.audio_move = true;
    this.aduio_move_offset = this.ignis.project.audio_offset;
    this.aduio_move_x = (e.pageX - this.left);
}

IgnisTimeline.prototype.onAudioMouseMove = function (e)
{
    if (!this.audio_move) return;

    var d = (e.pageX - this.left) - this.aduio_move_x;
    this.ignis.project.audio_offset = this.aduio_move_offset + d * this.scale;
    
    this.onAudioRendered();
}

IgnisTimeline.prototype.onProjectUpdated = function ()
{
    // create or update lements
    const project = this.ignis.project;

    var uids = [];
    for (var i in project.timeline) {
        var n = project.timeline[i];

        var el = $('#tlimg-'+n.uid);
        if (el.length == 0) {
            el = $('<div></div>');
            el.addClass('img');
            el.addClass('timeline-img');
            el.attr('id', 'tlimg-' + n.uid);
            el.attr('uid', n.uid);
            el.attr('idx', i);
            project.timeline[i].tex_loaded = this.updateNodeElementImage(el, n, project.leds);
            el.css('left', Math.round(n.start / this.scale) + 'px');
            el.css('width', Math.round(n.duration / this.scale) + 'px');
            el.on('click', $.proxy(this.onImageClick, this));
            el.on('dblclick', $.proxy(this.onImageDoubleClick, this));
            el.on('mousedown', $.proxy(this.onImageMouseDown, this));
            el.on('mousemove', $.proxy(this.onImageMouseMove, this));
            project.timeline[i].el = el;
            $('#timeline-images').append(el);
            if (n.activate) {
                delete(n.activate);
                el.addClass('active');
            }
            el.on('contextmenu', $.proxy(function (e) {
                if (this.suppress_image_context_menu) {
                    e.preventDefault();
                    this.suppress_image_context_menu = false;
                    return;
                }
                var dt = Date.now() - this.timelineSwitchTime;
                if (dt < 1000) return;
                $('.delete-popup').remove();
                var del = $('<div class="delete-popup"></div>');
                del.append('<div class="question">Remove from timeline?</div>');
                var yes = $('<div class="yes">Yes</div>');
                var no = $('<div class="no">No</div>');
                del.append(yes);
                del.append(no);
                del.css('left', (e.pageX - this.left) + 'px');
                del.css('top', e.pageY + 'px');
                yes.attr('idx', $(e.target).attr('idx'));
                yes.attr('uid', $(e.target).attr('uid'));
                $('body').append(del);
                no.on('click', $.proxy(function (e) {
                    $('.delete-popup').remove();
                }, this));
                yes.on('click', $.proxy(function (e) {
                    var uid = parseInt($(e.currentTarget).attr('uid'));
                    var selectedNodes = this.getSelectedNodes();
                    if (selectedNodes.length > 1 && this.isNodeSelected(this.ignis.project.currentTimeline, uid)) {
                        $('.delete-popup').remove();
                        this.removeSelected();
                    } else {
                        var i = this.findNodeIndexByUid(this.ignis.project.currentTimeline, uid);
                        $('.delete-popup').remove();
                        if (i >= 0) this.ignis.project.deleteNode(i);
                        this.clearSelectionVisuals();
                        this.updateInactiveTimelines();
                    }
                }, this));
            }, this));
        } else {
            project.timeline[i].tex_loaded = this.updateNodeElementImage(el, n, project.leds);
            el.css('left', Math.round(n.start / this.scale) + 'px');
            el.css('width', Math.round(n.duration / this.scale) + 'px');
            el.attr('idx', i);
        }

        uids.push(n.uid);
    }

    // remove non-existant elements
    $('.timeline-img').each(function () {
        var uid = parseInt($(this).attr('uid'));
        if (uids.indexOf(uid) < 0) {
            $(this).remove();
        }
    });

    // update audio track
    //$('#timeline-audio').empty();
    this.onAudioRendered();

    // update switches
    this.updateTimelineSwitches();
    this.syncCurrentTimelineSelection();
    this.refreshSelectionVisuals();
    //setTimeout($.proxy(this.updateInactiveTimelines, this), 100);
}

IgnisTimeline.prototype.onImageMouseDown = function (e)
{
    this.mx = (e.pageX - this.left);
    this.my = e.pageY;

    if (e.button == 2) {
        return;
    }

    if (!$(e.target).hasClass('active') && !$(e.target).hasClass('selected') && !e.shiftKey) return;

    this.mdown = true;
    this.ox = e.offsetX;
    this.oy = e.offsetY;
    this.ow = $(e.target).width();
    this.oe = $(e.target).position().left + $(e.target).width();
    this.hover_element = $(e.target);
    this.hover_index = this.hover_element.attr('idx');

    if (e.shiftKey) {
        $('.timeline-img').removeClass('active selected');
        this.hover_element.addClass('active selected');
        this.editor_index = this.hover_index;
        this.selected_items = [this.hover_index];
        this.selected_nodes = [{
            timelineHash: this.ignis.project.currentTimeline,
            uid: parseInt(this.hover_element.attr('uid')),
            index: parseInt(this.hover_index),
        }];
        this.ignis.properties.editorSet(this.editor_index);
        this.hover_mode = 'stretch_timelines';
        this.stretch_source = {
            nodeIndex: parseInt(this.hover_index),
            timelineHash: this.ignis.project.currentTimeline,
            timelineIndex: this.getTimelineIndex(this.ignis.project.currentTimeline),
        };
        this.stretch_target_index = this.stretch_source.timelineIndex;
        this.hover_element.addClass('timeline-img-stretching');
        this.updateStretchTimelinePreview(this.stretch_target_index);
        return;
    }

    if ($(e.target).hasClass('selected') && this.getSelectedNodes().length > 1) {
        this.startSelectedNodesMove(e, $(e.target));
    } else {
        if (e.offsetX < 5) {
            this.hover_mode = 'left';
            this.cursor_helper_start.show();
            this.showZoom();
            var p = Math.round(this.position + ((e.pageX - this.left) - this.ox) * this.scale);
            this.updateZoom(p, (e.pageX - this.left) - this.ox);
        } else if (e.offsetX < $(e.target).width() - 5) {
            this.hover_mode = 'move';
            this.cursor_helper_start.show();
            this.cursor_helper_end.show();
        } else {
            this.hover_mode = 'right';
            this.cursor_helper_end.show();
            this.showZoom();
            var p = Math.round(this.position + ((e.pageX - this.left)) * this.scale);
            this.updateZoom(p - 1, (e.pageX - this.left) + (this.ow - this.ox));
        }
    }
}

IgnisTimeline.prototype.showZoom = function ()
{
    if (this.ignis.audio.envelopeBuffers.length <= 0) return;
    $('#timeline-zoom').show();
}

IgnisTimeline.prototype.hideZoom = function ()
{
    $('#timeline-zoom').hide();
}

IgnisTimeline.prototype.updateZoom = function (position, cx)
{
    position = position - this.ignis.project.audio_offset;
    if (!cx) {
        if (this.realMouseX) {
            cx = this.realMouseX;
        } else {
            cx = 0;
        }
    }
    var x = (cx - 101);
    if (x < 1) x = 1;
    if (x > window.innerWidth - 200) x = window.innerWidth - 200;
    $('#timeline-zoom').css('left', x + 'px');

    var canvas = $('#zoom-canvas')[0];
    var ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    var i = Math.floor(position / 10000);
    var pos = position - (i * 10000) - 100;

    if (i < this.ignis.audio.envelopeBuffers.length && this.ignis.audio.envelopeBuffers.length > 0 && i >= 0) {
        var buff = this.ignis.audio.envelopeBuffers[i];
        ctx.drawImage(buff, -pos, 0);

        if (pos >= 9800 && (i + 1) < this.ignis.audio.envelopeBuffers.length) {
            buff = this.ignis.audio.envelopeBuffers[i + 1];
            ctx.drawImage(buff, -pos + 10000, 0);
        }

        if (pos < 0 && (i - 1) >= 0) {
            buff = this.ignis.audio.envelopeBuffers[i - 1];
            ctx.drawImage(buff, -pos - 10000, 0);
        }

    }

    ctx.strokeStyle = "#ffffff";
    ctx.beginPath();
    ctx.moveTo(100.5, 0.5);
    ctx.lineTo(100.5, canvas.height + 0.5);
    ctx.stroke();

    /*ctx.font = "10px Arial";
    ctx.fillStyle = "red";
    ctx.fillText("position = " + position, 10, 10);
    ctx.fillText("pos = " + pos, 10, 20);
    ctx.fillText("i = " + i + " / " + this.ignis.audio.envelopeBuffers.length, 10, 30);*/
}

IgnisTimeline.prototype.onImageMouseMove = function (e)
{
    if (!$(e.target).hasClass('active') && !$(e.target).hasClass('selected')) {
        $(e.target).css('cursor', 'pointer');
        return;
    }

    if ($(e.target).hasClass('selected') && this.getSelectedNodes().length > 1) {
        $(e.target).css('cursor', 'grab');
        return;
    }

    if (e.offsetX < 5) {
        $(e.target).css('cursor', 'col-resize');
    } else if (e.offsetX < $(e.target).width() - 5) {
        $(e.target).css('cursor', 'grab');
    } else {
        $(e.target).css('cursor', 'col-resize');
    }

    this.drawTimescale();
}

IgnisTimeline.prototype.onImageDoubleClick = function (e)
{
    if (this.ignis.library.selected_item) this.ignis.library.deselect();
}

IgnisTimeline.prototype.onImageClick = function (e)
{
    if (this.selected_items == undefined) this.selected_items = [];
    if ((e.pageX - this.left) != this.mx) return;

    var uid = parseInt($(e.target).attr('uid'));
    var timelineHash = this.ignis.project.currentTimeline;

    if (e.ctrlKey) {
        this.selectNodeRef(timelineHash, uid, true);
    } else {
        this.selectNodeRef(timelineHash, uid, false);
    }
}

IgnisTimeline.prototype.onInactiveImageMouseDown = function (e)
{
    if (e.button != 0) return;

    var el = $(e.currentTarget);
    if (!el.hasClass('selected')) return;
    if (this.getSelectedNodes().length <= 1) return;

    this.startSelectedNodesMove(e, el);
    e.preventDefault();
    e.stopPropagation();
}

IgnisTimeline.prototype.selectNode = function (uid)
{
    var img_el = $('.timeline-img[uid='+uid+']');
    var idx = parseInt(img_el.attr('idx'));
    this.clearSelectionVisuals();
    this.selected_nodes = [{
        timelineHash: this.ignis.project.currentTimeline,
        uid: uid,
        index: idx,
    }];
    this.syncCurrentTimelineSelection();
    this.refreshSelectionVisuals();
}

IgnisTimeline.prototype.play = function ()
{
    $('#play-button').hide();
    $('#pause-button').show();
    this.ignis.audio.play(this.cursor_position);
}

IgnisTimeline.prototype.pause = function ()
{
    $('#play-button').show();
    $('#pause-button').hide();
    this.ignis.audio.pause();
}

IgnisTimeline.prototype.rewind = function ()
{
    var ls = this.ignis.audio.started;
    if (ls) this.ignis.audio.pause();
    this.cursor_position -= 5000;
    if (this.cursor_position < 0) this.cursor_position = 0;
    if (ls) this.ignis.audio.play(this.cursor_position);
}

IgnisTimeline.prototype.rewindFull = function ()
{
    var ls = this.ignis.audio.started;
    if (ls) this.ignis.audio.pause();
    this.cursor_position = 0;
    if (ls) this.ignis.audio.play(this.cursor_position);
}

IgnisTimeline.prototype.forward = function ()
{
    var ls = this.ignis.audio.started;
    if (ls) this.ignis.audio.pause();
    this.cursor_position += 5000;
    if (ls) this.ignis.audio.play(this.cursor_position);
}

IgnisTimeline.prototype.forwardFull = function ()
{
    var ls = this.ignis.audio.started;
    if (ls) this.ignis.audio.pause();
    
    var max = 0;
    for (var i in this.ignis.project.timeline) {
        var n = this.ignis.project.timeline[i];
        var end = n.start + n.duration;
        if (max < end) max = end;
    }
    
    this.cursor_position = max;
    if (ls) this.ignis.audio.play(this.cursor_position);
}

IgnisTimeline.prototype.setScale = function (i)
{
    this.scale = i;
    this.real_width = $('#timeline-timeline').width() * this.scale;
}

IgnisTimeline.prototype.resize = function (e)
{
    this.setScale(this.scale);
    this.canvas.width = $('#timeline-canvas').width();
    this.canvas.height = $('#timeline-canvas').height();
    this.drawTimescale();
    this.onProjectUpdated();
    this.fixVscroll();
}

IgnisTimeline.prototype.onMouseWheel = function (e)
{
    var s = Math.abs(e.originalEvent.wheelDelta) / config.timeline.scale_factor;

    var targetScale = this.scale;
    var targetPosition = this.position;

    if (e.originalEvent.wheelDelta < 0) {
        targetScale *= s;
    } else {
        targetScale /= s;
    }
    if (targetScale < config.timeline.min_scale) targetScale = config.timeline.min_scale;
    if (targetScale > config.timeline.max_scale) targetScale = config.timeline.max_scale;

    var cp = this.position + (e.pageX - this.left) * this.scale;
    targetPosition = -((e.pageX - this.left) * targetScale - cp);
    if (targetPosition < 0) targetPosition = 0;

    this.setScale(targetScale);
    this.position = targetPosition;

    this.drawTimescale();
    this.onProjectUpdated();
    $('#timeline-scroll').css('left', (- this.position / this.scale) + 'px');
}

IgnisTimeline.prototype.zoom = function (s)
{
    var epageX = (this.cursor_position - this.position) / this.scale;
    var targetScale = this.scale;
    var targetPosition = this.position;

    if (s < 0) {
        targetScale *= Math.abs(s);
    } else {
        targetScale /= Math.abs(s);
    }
    if (targetScale < 1) targetScale = 1;
    if (targetScale > 1000) targetScale = 1000;

    var cp = this.position + epageX * this.scale;
    targetPosition = -(epageX * targetScale - cp);
    if (targetPosition < 0) targetPosition = 0;

    this.setScale(targetScale);
    this.position = targetPosition;

    this.drawTimescale();
    this.onProjectUpdated();
    $('#timeline-scroll').css('left', (- this.position / this.scale) + 'px');
}

IgnisTimeline.prototype.zoomIn = function (e)
{
    this.zoom(1.2);
}

IgnisTimeline.prototype.zoomOut = function (e)
{
    this.zoom(-1.2);
}

IgnisTimeline.prototype.zoomFit = function (e)
{
    if (this.ignis.project.timeline.length == 0) return;

    var min = this.ignis.project.timeline[0].start;
    var max = 0;

    for (var i in this.ignis.project.timeline) {
        var n = this.ignis.project.timeline[i];
        if (min > n.start) min = n.start;
        if (max < n.end) max = n.end;
    }

    min -= 1000;
    max += 1000;
    if (min < 0) min = 0;
    var d = max - min;
    var w = $('#timeline-timeline').width();

    this.position = min;
    this.setScale(d / w);
    //this.cursor_position = this.position + d / 2;

    this.drawTimescale();
    this.onProjectUpdated();
    $('#timeline-scroll').css('left', (- this.position / this.scale) + 'px');
}

IgnisTimeline.prototype.onDragOverTimeline = function (e)
{
    var i = e.pageY - $('#timeline-images-wrapper').offset().top + this.vscroll;
    i = Math.floor(i / this.timelineHeight);
    var c = 0;
    for (var hash in this.ignis.project.timelines) {
        if (c == i && this.ignis.project.currentTimeline != hash) {
            this.switchTimeline(hash);
            return;
        }
        c++;
    }
}

IgnisTimeline.prototype.onDropTimeline = function (e)
{
    const project = this.ignis.project;

    var etype = e.originalEvent.dataTransfer.getData('itype');
    if (etype == 'libimg' || etype == 'libeffect') {
        var time = Math.floor(this.position + (e.originalEvent.pageX - this.left) * this.scale);
        var duration = config.timeline.default_duration;
        var lib = this.ignis.library.drag_item;

        if (etype == 'libimg' && e.originalEvent.shiftKey && project.timelinesCount() > 1) {
            this.addImageAcrossTimelines(lib, time, duration);
            return true;
        }
        if (etype == 'libeffect' && e.originalEvent.shiftKey && project.timelinesCount() > 1) {
            this.addEffectAcrossTimelines(lib, time, duration);
            return true;
        }

        project.historyPush();
        //project.addNode(lib.hash, lib.path, time, duration);
        project.addNodeIq(lib.hash, lib.path, time, duration);
        this.selectLatestNode();

        return true;
    }

    if (etype == 'libaudio') {
        var ls = this.ignis.audio.started;
        this.ignis.audio.pause();

        var time = Math.floor(this.position + (e.originalEvent.pageX - this.left) * this.scale);
        var lib = this.ignis.library.drag_item;
        
        this.ignis.audio.loadFile(lib.path, (ls ? this.cursor_position : false));
        this.ignis.project.audio = lib.path;
        this.ignis.project.audio_hash = lib.hash;

        return true;
    }

    return false;
}

IgnisTimeline.prototype.getTimelineHashes = function ()
{
    return Object.keys(this.ignis.project.timelines);
}

IgnisTimeline.prototype.getTimelineIndex = function (hash)
{
    var hashes = this.getTimelineHashes();
    return hashes.indexOf(hash);
}

IgnisTimeline.prototype.getTimelineIndexAtPageY = function (pageY)
{
    var y = pageY - $('#timeline-images-wrapper').offset().top + this.vscroll;
    var idx = Math.floor(y / this.timelineHeight);
    var max = this.getTimelineHashes().length - 1;
    if (idx < 0) idx = 0;
    if (idx > max) idx = max;
    return idx;
}

IgnisTimeline.prototype.isTimelineSelectionSurface = function (target)
{
    var el = $(target);
    if (el.closest('.timeline-img, .img-inactive, #timeline-cursor, #timeline-audio, .audio-scroller').length > 0) return false;
    if (el.closest('#timeline-images-wrapper, #timelines-inactive').length > 0) return true;
    return el.attr('id') == 'timeline-images-wrapper' || el.attr('id') == 'timelines-inactive';
}

IgnisTimeline.prototype.getSelectionPoint = function (e)
{
    var wrapper = $('#timeline-images-wrapper');
    var offset = wrapper.offset();
    return {
        pageX: e.pageX,
        pageY: e.pageY,
        x: (e.pageX - this.left) + (this.position / this.scale),
        y: (e.pageY - offset.top) + this.vscroll
    };
}

IgnisTimeline.prototype.ensureSelectionBox = function ()
{
    if (this.selection_box && this.selection_box.length) return;
    this.selection_box = $('<div id="timeline-selection-box"></div>');
    $('#timeline-images-wrapper').append(this.selection_box);
}

IgnisTimeline.prototype.removeSelectionBox = function ()
{
    if (this.selection_box) {
        this.selection_box.remove();
    }
    this.selection_box = null;
    this.selection_start = null;
    this.selection_current = null;
}

IgnisTimeline.prototype.updateSelectionBox = function (e)
{
    if (!this.selection_start) return;
    this.ensureSelectionBox();
    this.selection_current = this.getSelectionPoint(e);

    var left = Math.min(this.selection_start.x, this.selection_current.x);
    var right = Math.max(this.selection_start.x, this.selection_current.x);
    var top = Math.min(this.selection_start.y, this.selection_current.y);
    var bottom = Math.max(this.selection_start.y, this.selection_current.y);

    this.selection_box.css({
        left: left + 'px',
        top: (top - this.vscroll) + 'px',
        width: Math.max(1, right - left) + 'px',
        height: Math.max(1, bottom - top) + 'px'
    });
}

IgnisTimeline.prototype.getTimelineDataForHash = function (hash)
{
    if (!this.ignis.project.timelines[hash]) return [];
    if (hash == this.ignis.project.currentTimeline) return this.ignis.project.timeline;
    return this.ignis.project.timelines[hash].data || [];
}

IgnisTimeline.prototype.addNodeSelectionRef = function (timelineHash, uid, index)
{
    if (this.isNodeSelected(timelineHash, uid)) return;
    this.selected_nodes.push({ timelineHash: timelineHash, uid: uid, index: index });
}

IgnisTimeline.prototype.getNodeElement = function (timelineHash, uid)
{
    if (timelineHash == this.ignis.project.currentTimeline) {
        return $('.timeline-img[uid=' + uid + ']');
    }
    return $('.img-inactive[timeline-hash=' + timelineHash + '][uid=' + uid + ']');
}

IgnisTimeline.prototype.startSelectedNodesMove = function (e, element)
{
    var nodes = this.getSelectedNodes();
    if (!nodes || nodes.length == 0) return;

    this.mdown = true;
    this.mx = (e.pageX - this.left);
    this.my = e.pageY;
    this.hover_mode = 'nodes_move';
    this.hover_element = element;
    this.suppress_image_context_menu = (e.button == 2);

    this.nodes_move = {
        items: [],
        minStart: null
    };

    for (var i in nodes) {
        var selected = nodes[i];
        var data = this.getTimelineDataForHash(selected.timelineHash);
        var node = data[selected.index];
        if (!node) continue;
        var el = this.getNodeElement(selected.timelineHash, selected.uid);
        this.nodes_move.items.push({
            timelineHash: selected.timelineHash,
            uid: selected.uid,
            index: selected.index,
            start: node.start,
            end: node.end,
            duration: node.duration,
            element: el
        });
        if (this.nodes_move.minStart === null || node.start < this.nodes_move.minStart) {
            this.nodes_move.minStart = node.start;
        }
    }

    if (this.nodes_move.items.length == 0) {
        this.nodes_move = null;
        this.mdown = false;
        this.hover_mode = 'none';
    }
}

IgnisTimeline.prototype.getNodesMoveDelta = function (dx)
{
    if (!this.nodes_move) return 0;
    var delta = dx * this.scale;
    if (this.nodes_move.minStart !== null && this.nodes_move.minStart + delta < 0) {
        delta = -this.nodes_move.minStart;
    }
    return delta;
}

IgnisTimeline.prototype.previewSelectedNodesMove = function (delta)
{
    if (!this.nodes_move) return;

    for (var i in this.nodes_move.items) {
        var item = this.nodes_move.items[i];
        if (!item.element || item.element.length == 0) continue;
        item.element.css('left', Math.round((item.start + delta) / this.scale) + 'px');
    }
}

IgnisTimeline.prototype.finishSelectedNodesMove = function (delta)
{
    if (!this.nodes_move) return;

    if (delta != 0) {
        var project = this.ignis.project;
        var originalTimeline = project.currentTimeline;
        var touched = {};

        project.historyPush();

        for (var i in this.nodes_move.items) {
            var item = this.nodes_move.items[i];
            var idx = this.findNodeIndexByUid(item.timelineHash, item.uid);
            if (idx < 0) continue;
            var data = this.getTimelineDataForHash(item.timelineHash);
            var node = data[idx];
            if (!node) continue;
            node.start = item.start + delta;
            node.end = node.start + node.duration;
            touched[item.timelineHash] = true;
        }

        for (var hash in touched) {
            if (!project.timelines[hash]) continue;
            project.switchTimeline(hash);
            project.recalculate();
        }

        project.switchTimeline(originalTimeline);
    }

    this.nodes_move = null;
    this.updateInactiveTimelines();
    this.syncCurrentTimelineSelection();
    this.refreshSelectionVisuals();
    this.onProjectUpdated();
}

IgnisTimeline.prototype.selectNodesInSelection = function (additive)
{
    if (!this.selection_start || !this.selection_current) return;

    var left = Math.min(this.selection_start.x, this.selection_current.x);
    var right = Math.max(this.selection_start.x, this.selection_current.x);
    var top = Math.min(this.selection_start.y, this.selection_current.y);
    var bottom = Math.max(this.selection_start.y, this.selection_current.y);
    var startTime = left * this.scale;
    var endTime = right * this.scale;
    var startRow = Math.floor(top / this.timelineHeight);
    var endRow = Math.floor(bottom / this.timelineHeight);
    var hashes = this.getTimelineHashes();

    if (!additive) {
        this.clearSelectionVisuals();
    }

    if (startRow < 0) startRow = 0;
    if (endRow >= hashes.length) endRow = hashes.length - 1;

    for (var row = startRow; row <= endRow; row++) {
        var hash = hashes[row];
        var data = this.getTimelineDataForHash(hash);
        for (var i in data) {
            var node = data[i];
            if (!node) continue;
            if (node.end < startTime || node.start > endTime) continue;
            this.addNodeSelectionRef(hash, node.uid, parseInt(i));
        }
    }

    if (hashes[startRow] && !this.selected_nodes.length) {
        this.switchTimeline(hashes[startRow]);
    } else if (this.selected_nodes.length) {
        this.switchTimeline(this.selected_nodes[0].timelineHash);
    }

    this.syncCurrentTimelineSelection();
    this.refreshSelectionVisuals();
}

IgnisTimeline.prototype.updateStretchTimelinePreview = function (targetIndex)
{
    if (!this.stretch_source) return;

    this.stretch_target_index = targetIndex;
    $('.timeline-switch').removeClass('stretch-target');
    var start = Math.min(this.stretch_source.timelineIndex, targetIndex);
    var end = Math.max(this.stretch_source.timelineIndex, targetIndex);
    var hashes = this.getTimelineHashes();
    for (var i = start; i <= end; i++) {
        $('.timeline-switch[hash=' + hashes[i] + ']').addClass('stretch-target');
    }
}

IgnisTimeline.prototype.findNodeIndexByUid = function (timelineHash, uid)
{
    var timeline = this.ignis.project.timelines[timelineHash];
    if (!timeline) return -1;

    for (var i in timeline.data) {
        if (timeline.data[i] && timeline.data[i].uid == uid) {
            return parseInt(i);
        }
    }

    return -1;
}

IgnisTimeline.prototype.isNodeSelected = function (timelineHash, uid)
{
    for (var i in this.selected_nodes) {
        var selected = this.selected_nodes[i];
        if (selected.timelineHash == timelineHash && selected.uid == uid) return true;
    }
    return false;
}

IgnisTimeline.prototype.applyInactiveSelectionState = function (element, timelineHash, uid)
{
    if (this.isNodeSelected(timelineHash, uid)) {
        element.addClass('selected');
    } else {
        element.removeClass('selected');
    }
}

IgnisTimeline.prototype.selectNodeRef = function (timelineHash, uid, additive)
{
    var idx = this.findNodeIndexByUid(timelineHash, uid);
    if (idx < 0) return;

    if (additive && this.isNodeSelected(timelineHash, uid)) {
        this.removeNodeRefSelection(timelineHash, uid);
        this.syncCurrentTimelineSelection();
        this.refreshSelectionVisuals();
        return;
    }

    if (!additive) {
        this.clearSelectionVisuals();
    }

    if (!this.isNodeSelected(timelineHash, uid)) {
        this.selected_nodes.push({ timelineHash: timelineHash, uid: uid, index: idx });
    }

    if (additive) {
        this.syncCurrentTimelineSelection();
        this.refreshSelectionVisuals();
    } else {
        this.setActiveNodeRef(timelineHash, uid);
    }
}

IgnisTimeline.prototype.setActiveNodeRef = function (timelineHash, uid)
{
    var idx = this.findNodeIndexByUid(timelineHash, uid);
    if (idx < 0) return;

    this.selected_nodes = this.selected_nodes.filter(function (selected) {
        return !(selected.timelineHash == timelineHash && selected.uid == uid);
    });
    this.selected_nodes.unshift({ timelineHash: timelineHash, uid: uid, index: idx });

    if (this.ignis.project.currentTimeline != timelineHash) {
        this.switchTimeline(timelineHash);
        return;
    }

    this.syncCurrentTimelineSelection();
    this.editor_index = idx;
    this.refreshSelectionVisuals();
}

IgnisTimeline.prototype.removeNodeRefSelection = function (timelineHash, uid)
{
    this.selected_nodes = this.selected_nodes.filter(function (selected) {
        return !(selected.timelineHash == timelineHash && selected.uid == uid);
    });
}

IgnisTimeline.prototype.clearSelectionVisuals = function ()
{
    this.selected_nodes = [];
    this.selected_items = [];
    this.editor_index = null;
    $('.timeline-img').removeClass('active selected');
    $('.img-inactive').removeClass('selected');
}

IgnisTimeline.prototype.syncCurrentTimelineSelection = function ()
{
    var project = this.ignis.project;
    this.selected_items = [];
    this.editor_index = null;

    for (var i in this.selected_nodes) {
        var selected = this.selected_nodes[i];
        selected.index = this.findNodeIndexByUid(selected.timelineHash, selected.uid);
        if (selected.index < 0) continue;
        if (selected.timelineHash == project.currentTimeline) {
            this.selected_items.push(selected.index);
            if (this.selected_nodes.length == 1) this.editor_index = selected.index;
        }
    }

    this.editor_multi = this.selected_nodes.length > 1;
}

IgnisTimeline.prototype.refreshSelectionVisuals = function ()
{
    var project = this.ignis.project;
    $('.timeline-img').removeClass('active selected');
    $('.img-inactive').removeClass('selected');

    for (var i in this.selected_nodes) {
        var selected = this.selected_nodes[i];
        if (selected.timelineHash == project.currentTimeline) {
            $('.timeline-img[uid=' + selected.uid + ']').addClass('selected');
            if (this.editor_index == selected.index) {
                $('.timeline-img[uid=' + selected.uid + ']').addClass('active');
            }
        } else {
            $('.img-inactive[timeline-hash=' + selected.timelineHash + '][uid=' + selected.uid + ']').addClass('selected');
        }
    }

    if (this.editor_index !== null) {
        this.ignis.properties.editorSet(this.editor_index);
    } else {
        this.ignis.properties.editorSet(null);
    }
}

IgnisTimeline.prototype.getSelectedNodes = function ()
{
    if (this.selected_nodes && this.selected_nodes.length > 0) {
        this.syncCurrentTimelineSelection();
        return this.selected_nodes.filter(function (selected) {
            return selected.index >= 0;
        });
    }

    var nodes = [];
    for (var i of this.selected_items) {
        i = parseInt(i);
        if (isNaN(i)) continue;
        if (!this.ignis.project.timeline[i]) continue;
        nodes.push({
            timelineHash: this.ignis.project.currentTimeline,
            uid: this.ignis.project.timeline[i].uid,
            index: i,
        });
    }
    return nodes;
}

IgnisTimeline.prototype.onInactiveImageClick = function (e)
{
    e.preventDefault();
    e.stopPropagation();
    var el = $(e.currentTarget);
    var timelineHash = el.attr('timeline-hash');
    var uid = parseInt(el.attr('uid'));
    var additive = e.ctrlKey;

    this.selectNodeRef(timelineHash, uid, additive);
}

IgnisTimeline.prototype.finishStretchTimelineImage = function (targetIndex)
{
    $('.timeline-img').removeClass('timeline-img-stretching');
    $('.timeline-switch').removeClass('stretch-target');

    if (!this.stretch_source) return;

    var source = this.stretch_source;
    this.stretch_source = null;
    this.stretch_target_index = null;

    if (targetIndex === source.timelineIndex || targetIndex < 0) {
        return;
    }

    var hashes = this.getTimelineHashes();
    var start = Math.min(source.timelineIndex, targetIndex);
    var end = Math.max(source.timelineIndex, targetIndex);
    var range = hashes.slice(start, end + 1);
    var sourceNode = this.ignis.project.timelines[source.timelineHash].data[source.nodeIndex];
    if (!sourceNode || range.length <= 1) return;

    if (sourceNode.type == 'effect') {
        var effectLib = this.getEffectLibFromNode(sourceNode);
        this.addEffectAcrossTimelineRange(effectLib, range, sourceNode.start, sourceNode.duration, source.timelineHash, sourceNode.uid);
        return;
    }

    var lib = this.ignis.library.getImageByHash(sourceNode.hash);
    if (!lib) return;
    this.addImageAcrossTimelineRange(lib, range, sourceNode.start, sourceNode.duration, source.timelineHash, sourceNode.uid);
}

IgnisTimeline.prototype.getEffectLibFromNode = function (node)
{
    if (!node || node.type != 'effect') return null;
    return {
        type: 'effect',
        hash: node.hash,
        id: node.effectId,
        name: node.effectName,
        speed: node.effectSpeed,
        intensity: node.effectIntensity,
        size: node.effectSize,
        paletteId: node.effectPaletteId,
        colors: (node.effectColors || ['#ff6000', '#00b4ff', '#ffffff']).slice(0)
    };
}

IgnisTimeline.prototype.addImageAcrossTimelines = function (lib, time, duration)
{
    if (!lib || !lib.path) return;

    var timeline_hashes = Object.keys(this.ignis.project.timelines);
    if (timeline_hashes.length <= 1) return;

    this.addImageAcrossTimelineRange(lib, timeline_hashes, time, duration);
}

IgnisTimeline.prototype.addEffectAcrossTimelines = function (lib, time, duration)
{
    if (!lib || lib.type != 'effect') return;

    var timeline_hashes = Object.keys(this.ignis.project.timelines);
    if (timeline_hashes.length <= 1) return;

    this.addEffectAcrossTimelineRange(lib, timeline_hashes, time, duration);
}

IgnisTimeline.prototype.addEffectAcrossTimelineRange = function (lib, timeline_hashes, time, duration, sourceTimelineHash, sourceUid)
{
    if (!lib || lib.type != 'effect' || !timeline_hashes || timeline_hashes.length <= 1) return;

    var project = this.ignis.project;
    var original_timeline = project.currentTimeline;

    if (sourceTimelineHash && sourceUid !== undefined && project.timelines[sourceTimelineHash]) {
        project.timelines[sourceTimelineHash].data = project.timelines[sourceTimelineHash].data.filter(function (node) {
            return node && node.uid != sourceUid;
        });
        if (sourceTimelineHash == project.currentTimeline) {
            project.timeline = project.timelines[sourceTimelineHash].data;
        }
    }

    for (var i = 0; i < timeline_hashes.length; i++) {
        project.switchTimeline(timeline_hashes[i]);
        project.historyPush();
        this.ignis.library.drag_item = $.extend(true, {}, lib);
        project.addNodeIq(lib.hash, '', time, duration);
    }

    project.switchTimeline(original_timeline);
    this.deselect();
    this.updateInactiveTimelines();
    this.onProjectUpdated();
    if (this.ignis.preview) this.ignis.preview.nodeChanged();
}

IgnisTimeline.prototype.addImageAcrossTimelineRange = function (lib, timeline_hashes, time, duration, sourceTimelineHash, sourceUid)
{
    if (!lib || !lib.path || !timeline_hashes || timeline_hashes.length <= 1) return;

    this.sliceImageForTimelines(lib, timeline_hashes.length, $.proxy(function (slices) {
        if (!slices || slices.length == 0) return;

        var project = this.ignis.project;
        var original_timeline = project.currentTimeline;

        if (sourceTimelineHash && sourceUid !== undefined && project.timelines[sourceTimelineHash]) {
            project.timelines[sourceTimelineHash].data = project.timelines[sourceTimelineHash].data.filter(function (node) {
                return node && node.uid != sourceUid;
            });
            if (sourceTimelineHash == project.currentTimeline) {
                project.timeline = project.timelines[sourceTimelineHash].data;
            }
        }

        for (var i = 0; i < timeline_hashes.length; i++) {
            var slice = slices[i];
            if (!slice) continue;

            project.switchTimeline(timeline_hashes[i]);
            project.historyPush();
            project.addNodeIq(slice.hash, slice.path, time, duration);
        }

        project.switchTimeline(original_timeline);
        this.deselect();
        this.ignis.library.genThumbs();
        this.updateInactiveTimelines();
        this.onProjectUpdated();
    }, this));
}

IgnisTimeline.prototype.sliceImageForTimelines = function (lib, count, callback)
{
    var img = new Image();

    img.onload = $.proxy(function () {
        var slices = [];
        var canvas = document.createElement('canvas');
        var ctx = canvas.getContext('2d');
        var tmpdir = ignis_dir('tmp');

        for (var i = 0; i < count; i++) {
            var sy = Math.floor((img.height * i) / count);
            var ey = Math.floor((img.height * (i + 1)) / count);
            var sh = Math.max(1, ey - sy);
            var sw = img.width;

            canvas.width = sw;
            canvas.height = sh;
            ctx.clearRect(0, 0, sw, sh);
            ctx.drawImage(img, 0, sy, sw, sh, 0, 0, sw, sh);

            var data = canvas.toDataURL('image/png').replace(/^data:image\/png;base64,/, '');
            var slice_path = tmpdir + path.sep + lib.hash + '_timeline_' + (i + 1) + '_of_' + count + '_' + Date.now() + '.png';
            fs.writeFileSync(slice_path, Buffer.from(data, 'base64'));

            var hash = this.ignis.library.addFile(slice_path);
            if (fs.existsSync(slice_path)) fs.unlinkSync(slice_path);

            var item = this.ignis.library.getImageByHash(hash);
            if (item) {
                var thumb_path = this.ignis.library.getThumbFilePath(hash);
                var thumb_data = canvas.toDataURL('image/jpeg', 0.92).replace(/^data:image\/jpeg;base64,/, '');
                fs.writeFileSync(thumb_path, Buffer.from(thumb_data, 'base64'));
                ignis_cache.has_thumb[hash] = true;

                item.resolution = { w: sw, h: sh, r: sw / sh };
                item.thumb = true;
                item.generated = true;
                this.ignis.library.save();
                $('.library-img[hash=' + hash + ']').remove();
                slices.push({ hash: hash, path: item.path });
            }
        }

        callback(slices);
    }, this);

    img.onerror = function () {
        callback([]);
    };

    img.src = lib.path;
}

IgnisTimeline.prototype.getImageAt = function(pos)
{
    const project = this.ignis.project;

    for (var i in project.timeline) {
        var n = project.timeline[i];

        if (n.start <= pos && n.end >= pos) {
            return n.hash;
        }
    }

    return null;
}

IgnisTimeline.prototype.getNodeAt = function(pos)
{
    const project = this.ignis.project;

    for (var i in project.timeline) {
        var n = project.timeline[i];
        if (n == null || n == undefined) continue;

        if (n.start <= pos && n.end >= pos) {
            return n;
        }
    }

    return null;
}

IgnisTimeline.prototype.getNodeAtEx = function(hash, pos)
{
    const project = this.ignis.project;

    if (project.currentTimeline == hash) {
        return this.getNodeAt(pos);
    }

    for (var i in project.timelines[hash].data) {
        var n = project.timelines[hash].data[i];
        if (n == null || n == undefined) continue;

        if (n.start <= pos && n.end >= pos) {
            return n;
        }
    }

    return null;
}

IgnisTimeline.prototype.imageThumbPath = function (hash)
{
    var tpath = ignis_thumbdir() + path.sep + hash + '.jpg';
    tpath = tpath.replace(/\\/g, '/');

    return tpath;
}

IgnisTimeline.prototype.image = function (hash)
{
    if (!this.cache[hash]) {
        this.cache[hash] = new Image();
        this.cache[hash].src = ignis_thumbdir() + path.sep + hash + '.jpg';
    }

    return this.cache[hash];
}

IgnisTimeline.prototype.drawTimescale = function()
{
    var ctx = this.ctx;
    
    ctx.strokeStyle = '#ffffff40';
    ctx.font = '13px "Lucida Console", Monaco, monospace';
    ctx.fillStyle = '#ffffff';

    var w = this.canvas.width;
    var h = this.canvas.height;

    var ts = Math.floor((w * this.scale) / 1000);
    // clear
    ctx.clearRect(0, 0, w, h);

    // image background
    var th = h - 85;
    var lh = this.timelineHeight;
    var tc = Math.ceil(th / lh);

    ctx.fillStyle = '#222222';
    ctx.fillRect(0, 15, w, h - 85);

    var tli = 0;
    for (var i in this.ignis.project.timelines) {
        if (tli % 2 == 0 /*&& this.ignis.project.currentTimeline != i*/) {
            tli++;
            continue;
        }
        ctx.fillStyle = "#333333";
        //if (this.ignis.project.currentTimeline == i) ctx.fillStyle = "#FF0000";
        var ty = 15 + tli * this.timelineHeight - this.vscroll;
        //var h = y + this.timelineHeight;
        //if (y < 15) y = 15;
        ctx.fillRect(0, ty, w, this.timelineHeight);
        tli++;
    }


    // soundtrack background
    ctx.fillStyle = '#111111';
    ctx.fillRect(0, h - 70, w, 70); //ctx.fillRect(0, 135, w, 70);

    ctx.fillStyle = '#ffffff';

    // hours and five-minutes
    if (ts >= 1800) {
        ctx.strokeStyle = '#ffffffff';
        this.drawTimescaleResolution(300000);
        ctx.strokeStyle = '#ffffff40';
        this.drawTimescaleResolution(60000, 15);
    }

    // minutes & 10secs
    if (ts >= 180 && ts < 1800) {
        ctx.strokeStyle = '#ffffffff';
        this.drawTimescaleResolution(60000);
        ctx.strokeStyle = '#ffffff40';
        this.drawTimescaleResolution(10000, 15);
    }

    // 10secs & secs
    if (ts >= 20 && ts < 180) {
        ctx.strokeStyle = '#ffffffff';
        this.drawTimescaleResolution(10000);
        ctx.strokeStyle = '#ffffff40';
        this.drawTimescaleResolution(1000, 15);
    }

    // secs & 100mil
    if (ts >= 0 && ts < 20) {
        ctx.strokeStyle = '#ffffffff';
        this.drawTimescaleResolution(1000);
        ctx.strokeStyle = '#ffffff40';
        this.drawTimescaleResolution(100, 15);
    }

    if (this.mdown) {
        if (this.hover_mode == 'left') {
            ctx.strokeStyle = '#ffffffff';
            //ctx.moveTo(x + 0.5, sy);
            //ctx.lineTo(x + 0.5, h);
        }
    }
}

IgnisTimeline.prototype.drawTimescaleResolution = function(resolution, sy)
{
    if (!sy) sy = 0;

    var ctx = this.ctx;
    var w = this.canvas.width;
    var h = this.canvas.height;

    var sp = Math.floor(this.position / resolution);
    var sl = Math.ceil((w * this.scale) / resolution) + 1;

    ctx.beginPath();
    for (var i = sp; i < sp + sl; i++) {
        var ms = Math.floor(i * resolution);
        var x = Math.floor((i * resolution - this.position) / this.scale);

        ctx.moveTo(x + 0.5, sy);
        ctx.lineTo(x + 0.5, h);

        if (sy == 0) {
            var s = Math.floor(ms / 1000);
            var m = Math.floor(s / 60);
            s = s - m * 60;
            if (s < 10) s = '0' + s;
            if (s < 60) {
                ctx.fillText(m + ':' + s, x + 3.5, 12);
            }
        }
    }
    ctx.stroke();
}

IgnisTimeline.prototype.reindex = function ()
{
    var project = this.ignis.project;
    for (var i in project.timeline) {
        var n = project.timeline[i];
        $('.timeline-img[uid='+n.uid+']').attr('idx', i);
    }
}

IgnisTimeline.prototype.deselect = function ()
{
    this.clearSelectionVisuals();
    this.ignis.properties.editorSet(null);
    this.onProjectUpdated();
}

IgnisTimeline.prototype.invalidateImage = function (hash)
{
    for (var i in this.ignis.project.timeline) {
        var n = this.ignis.project.timeline[i];

        if (n.hash == hash) {
            n.tex_loaded = false;
        }
    }

    this.ignis.preview.resetTexture();
}

IgnisTimeline.prototype.selectAllImages = function() {
    this.deselect(); // Zrušit výběr všech předchozích obrázků
    var selected = [];
    $('.timeline-img').each(function() {
        $(this).addClass('selected');
        selected.push(parseInt($(this).attr('idx')));
    });
    this.selected_items = selected; // Nastavení `selected_items`
}

IgnisTimeline.prototype.selectAllImages = function() {
    this.clearSelectionVisuals();
    for (var timelineHash in this.ignis.project.timelines) {
        var timeline = this.ignis.project.timelines[timelineHash].data;
        for (var i in timeline) {
            if (!timeline[i]) continue;
            this.selected_nodes.push({
                timelineHash: timelineHash,
                uid: timeline[i].uid,
                index: parseInt(i),
            });
        }
    }
    this.syncCurrentTimelineSelection();
    this.refreshSelectionVisuals();
}

IgnisTimeline.prototype.onGlobalKeyDown = function(e) {
    if (e.ctrlKey && e.key === 'a') { // Změna podmínky na CTRL + A
        e.preventDefault();
        this.selectAllImages();
    }
 

}
