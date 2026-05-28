function IgnisProperties(ignis)
{
    this.ignis = ignis;
    this.lastDrivesCount = 0;
    this.editor_hue_index = null;
    ignis.properties = this;
}

IgnisProperties.prototype.calcMaxFreq = function ()
{
    var max = Math.floor( (20500000 / ( this.ignis.project.leds * 24 ) ) / 100 ) * 100;
    max = Math.min(max, this.getMaxLineFrequency());

    var v = parseInt( $('#freq_input').val() );
    if (isNaN(v)) v = 0;
    if (v > max) {
        $('#freq_input').val(max);
    }
    $('#freq_input').attr('max', max);
}

IgnisProperties.prototype.getMaxLineFrequency = function ()
{
    return config.project.max_line_frequency || 2500;
}

IgnisProperties.prototype.clampLineFrequency = function (value)
{
    value = parseInt(value);
    if (isNaN(value)) value = 0;
    if (value < 0) value = 0;
    if (value > this.getMaxLineFrequency()) value = this.getMaxLineFrequency();
    return value;
}

IgnisProperties.prototype.rememberDeviceDefaults = function ()
{
    var project = this.ignis.project;
    var leds = parseInt(project.leds);
    if (isNaN(leds) || leds <= 0) return;

    if (this.ignis.userconf.get('last_leds') != leds) {
        this.ignis.userconf.set('last_leds', leds);
    }

    var selected = project.selected_device || (leds + '_C');
    if (this.ignis.userconf.get('last_selected_device') != selected) {
        this.ignis.userconf.set('last_selected_device', selected);
    }

    var acc = !!project.enable_accelerometer;
    if (this.ignis.userconf.get('last_enable_accelerometer') !== acc) {
        this.ignis.userconf.set('last_enable_accelerometer', acc);
    }
}

IgnisProperties.prototype.rememberLineFrequency = function (value)
{
    value = this.clampLineFrequency(value);
    if (this.ignis.userconf.get('last_line_frequency') != value) {
        this.ignis.userconf.set('last_line_frequency', value);
    }
}

IgnisProperties.prototype.updateDevices = function ()
{
    var devices = this.ignis.userconf.get('led_definitions');
    var selected = (this.ignis.project && this.ignis.project.selected_device) ? this.ignis.project.selected_device : $('#leds-count').val();

    $('#leds-count').empty();
    for (var n in devices) {
        var o = devices[n];
        $('#leds-count').append($('<option></option>').val(o.leds + '_' + (o.acc ? 'A' : 'N')).text(n));
    }
    if (config.project.pixel_count_custom) {
        var cl = this.ignis.userconf.get('custom_leds');
        $('#leds-count').append($('<option></option>').val(cl + '_C').text('Custom (' + cl + ' leds)'));
    }
    if (selected) {
        $('#leds-count').val(selected);
    }
    $('#leds-count').trigger('change');
}

IgnisProperties.prototype.autoEnumerate = function ()
{
    window.electronApi.listDrives().then((drives) => {
        if (this.lastDrivesCount != drives.length) {
            this.lastDrivesCount = drives.length;
            this.updateDrives();
        }
    });
}

IgnisProperties.prototype.updateDevicesData = function (data)
{
    $.post(config.project.pixel_url, $.proxy(function (data) {
        this.ignis.userconf.set('led_definitions', data);
        this.updateDevices();
    }, this));
}

IgnisProperties.prototype.init = function ()
{
    const project = this.ignis.project;

    this.updateDevices();
    this.updateDevicesData();

    this.updateDrives();

    $('#project-name').val(project.name);
    $('#leds-count').val(project.selected_device);
    $('#leds-count-input').val(project.leds);
    $('#leds-count-input').prop('readonly', true);
    $('#project-post').val(project.post);
    $('#leds-count').on('change keyup', $.proxy(function (e) {
        var acc = false;
        var tmp = $(e.target).val().split('_');
        var val = parseInt(tmp[0]);
        if (tmp.length == 2) acc = (tmp[1] != 'N');

        if (isNaN(val)) val = config.project.default_leds;

        this.ignis.project.enable_accelerometer = acc;
        this.ignis.project.selected_device = $(e.target).val();
        this.ignis.project.setLeds(val);
        if (e.originalEvent) {
            this.rememberDeviceDefaults();
        }
        this.calcMaxFreq();
    }, this));
    $('#leds-count-custom').on('click', $.proxy(function (e) {
        ignis_prompt('Please specify number of leds on custom stick:', this.ignis.userconf.get('custom_leds'), $.proxy(function (val) {
            val = parseInt(val);
            if (isNaN(val) || val <= 0) return;
            this.ignis.userconf.set('custom_leds', val);
            this.updateDevices();
            $('#leds-count').val(val+'_C');
            $('#leds-count').trigger('change');
            this.rememberDeviceDefaults();
        }, this));
    }, this));
    $('#leds-count-input').on('change keyup', $.proxy(function (e) {

        var leds = parseInt($(e.target).val());
        var oleds = $(e.target).val();
        if (isNaN(leds)) leds = config.project.default_leds;
        if (leds < 1) leds = 1;
        if (leds > 9999) leds = 9999;
        this.ignis.project.setLeds(leds);
        if (e.originalEvent) {
            this.rememberDeviceDefaults();
        }
        if (leds != oleds) {
            $('#leds-count-input').val(leds);
        }

    }, this));
    $('#project-name').on('change keyup keydown', $.proxy(function (e) {
        this.ignis.project.name = $('#project-name').val();
    }, this));
    $('#project-post').on('change', $.proxy(function (e) {
        this.ignis.project.post = $(e.target).val();
    }, this));
    /*$('#leds-count').on('blur', $.proxy(function (e) {
        if ($(e.target).val() != '?') {
            var leds = parseInt($(e.target).val());
            if (isNaN(leds)) leds = config.project.default_leds;
            if (leds < config.project.min_leds) leds = config.project.min_leds;
            if (leds > config.project.max_leds) leds = config.project.max_leds;
            this.ignis.project.setLeds(leds);
            $('#leds-count').val(leds);
        }
    }, this));*/

    $(document).on('mouseup', $.proxy(this.onMouseUp, this));
    $(document).on('click', $.proxy(this.onClick, this));

    $('.time-editor').on('click', '.plus', $.proxy(this.onTimePlus, this));
    $('.time-editor').on('click', '.minus', $.proxy(this.onTimeMinus, this));
    $('.time-editor').find('.secs,.mins,.millis').on('click', $.proxy(this.onTimeSelect, this));

    $('input[editor]').on('keydown keyup change keypress', $.proxy(this.onEditorInputChange, this));

    $(document).on('keypress', $.proxy(this.onKey, this));
    $(document).on('keydown', $.proxy(this.onKeyDown, this));

    $('#drives').on('change', $.proxy(this.updateDeviceFiles, this));

    setInterval($.proxy(this.updatePanel, this), 100);

    app_register_event('project_updated', $.proxy(this.projectUpdated, this));

    this.updateDeviceFiles();

    app_register_action('file_move_up', $.proxy(this.fileMoveUp, this));
    app_register_action('file_move_dn', $.proxy(this.fileMoveDn, this));
    app_register_action('file_delete', $.proxy(this.fileDelete, this));
    app_register_action('stretch_image', $.proxy(this.stretchImage, this));
    app_register_action('image_rotate_cw', $.proxy(function () { this.applyImageTransform({ rotate: 90 }); }, this));
    app_register_action('image_flip_h', $.proxy(function () { this.applyImageTransform({ flipH: true }); }, this));
    app_register_action('image_flip_v', $.proxy(function () { this.applyImageTransform({ flipV: true }); }, this));
    app_register_action('image_hue_apply', $.proxy(this.applyHueTransform, this));
    $('#image-hue-slider').on('input change', $.proxy(this.previewHueTransform, this));
    $('#image-hue-value').on('change keyup', $.proxy(this.onHueValueInput, this));

    $('#fit-image-count').on('keydown', $.proxy(function (e) {
        if (e.keyCode == 13) {
            e.preventDefault();
            this.stretchImage();
            return;
        }

        var i = parseInt($('#fit-image-count').val());
        if (isNaN(i)) i = 0;

        if ((e.keyCode == 189 || e.keyCode == 109) && i > 1) {
            e.preventDefault();
            $('#fit-image-count').val(i - 1);
            return;
        }
        if (e.keyCode == 187 || e.keyCode == 107) {
            e.preventDefault();
            $('#fit-image-count').val(i + 1);
            return;
        }
    }, this));

    app_register_action('properties_properties', $.proxy(this.switchProperties, this));
    app_register_action('properties_export', $.proxy(this.switchExport, this));

    $('[editor=accelerometer]').on('change', $.proxy(this.accelerometerChanged, this));

    if (!config.project.pixel_count_custom) $('#leds-count-input').hide();

    setInterval($.proxy(this.autoEnumerate, this), 1000);

    this.calcMaxFreq();

    setInterval($.proxy(this.updateFilenameInput, this), 500);

    $('#export-timeline-select').on('change', $.proxy(this.timelineSelectChanged, this));
    $('#export-technology').on('change', $.proxy(this.exportTechnologyChanged, this));

    $('#filename-editable-btn').on('click', $.proxy(this.filenameEditableToggle, this));

    this.projectUpdated();
}

IgnisProperties.prototype.filenameEditableToggle = function ()
{
    if ($('#filename-editable-btn').hasClass('active')) {
        $('#export-filename').prop('readonly', true);
        $('#filename-editable-btn').removeClass('active');
    } else {
        $('#export-filename').prop('readonly', false);
        $('#filename-editable-btn').addClass('active');
        $('#export-filename').focus();
        $('#export-filename').select();
    }
}

IgnisProperties.prototype.timelineSelectChanged = function ()
{
    this.ignis.timeline.switchTimeline($('#export-timeline-select').val());
}

IgnisProperties.prototype.updateFilenameInput = function ()
{
    $('#export-timeline-select').empty();
    var idx = 1;
    var sel_idx = 1;
    for (var i in this.ignis.project.timelines) {
        var el = $('<option value="' + i + '">' + (idx++) + '</option>');
        if (i == this.ignis.project.currentTimeline) {
            el.prop('selected', true);
            sel_idx = idx - 1;
        }
        $('#export-timeline-select').append(el);
    }

    if ($('#export-filename').prop('readonly')) {
        $('#export-filename').val(this.ignis.project.getProjectFilename(false, this.ignis.project.getExportExtension()));
    } else {
        var fn = $('#export-filename').val();
        //fn = fn.replace(/_[0-9]+(\.pix)?/i, '');
        //if (!fn.match(/_[0-9]+(\.pix)?/i)) {
            //fn = fn + '_' + sel_idx + '.pix';
        //}
        $('#export-filename').val(fn);
    }
}

IgnisProperties.prototype.switchProperties = function ()
{
    $('[action=properties_properties]').addClass('active');
    $('[action=properties_export]').removeClass('active');
    $('#properties-properties-box').show();
    $('#properties-export-box').hide();
}

IgnisProperties.prototype.switchExport = function ()
{
    $('[action=properties_properties]').removeClass('active');
    $('[action=properties_export]').addClass('active');
    $('#properties-properties-box').hide();
    $('#properties-export-box').show();
}

IgnisProperties.prototype.accelerometerChanged = function (e)
{
    if ($(e.delegateTarget).is(':checked')) {
        $('#picture-freq-field').show();
        $('#freq-field').hide();
    } else {
        $('#picture-freq-field').hide();
        $('#freq-field').show();
    }
}

IgnisProperties.prototype.fileMoveUp = function (force_fn)
{
    if ($('.file-record.selected').length == 0 && !force_fn) return;
    var fn = $('.file-record.selected').attr('fn');
    if (force_fn) fn = force_fn;
    if (!fn) return;

    var files = this.getFiles();
    var max_num = this.getMaxNum(files);

    var prev = '';
    for (var file of files) {
        if (file == fn) {
            break;
        }
        prev = file;
    }

    if (prev == '') return;

    var m = fn.match(/^([0-9]{3})_(.+)\.(pix|axp)$/i);
    var fn_num = max_num + 1;
    var fn_ext = (fn.match(/\.(pix|axp)$/i) || ['', 'pix'])[1].toLowerCase();
    var fn_name = fn.replace(/\.(pix|axp)$/i, '');
    if (m) {
        fn_num = parseInt(m[1]);
        fn_name = m[2];
        fn_ext = m[3].toLowerCase();
    }

    var m = prev.match(/^([0-9]{3})_(.+)\.(pix|axp)$/i);
    var prev_num = max_num + 1;
    var prev_ext = (prev.match(/\.(pix|axp)$/i) || ['', 'pix'])[1].toLowerCase();
    var prev_name = prev.replace(/\.(pix|axp)$/i, '');
    if (m) {
        prev_num = parseInt(m[1]);
        prev_name = m[2];
        prev_ext = m[3].toLowerCase();
    }

    this.renameFile(fn, ('' + prev_num).padStart(3, '0') + '_' + fn_name + '.' + fn_ext);
    this.renameFile(prev, ('' + fn_num).padStart(3, '0') + '_' + prev_name + '.' + prev_ext);

    this.updateDeviceFiles((force_fn ? ('' + fn_num).padStart(3, '0') + '_' + prev_name + '.' + prev_ext : ('' + prev_num).padStart(3, '0') + '_' + fn_name + '.' + fn_ext));
}

IgnisProperties.prototype.renameFile = function (from, to)
{
    var drive = $('#drives').val();
    if (!drive) {
        return;
    }

    from = drive + from;
    to = drive + to;

    fs.renameSync(from, to);
}

IgnisProperties.prototype.filesByNumber = function (files, exclude)
{
    var nn = 1000;
    var out = {};

    for (var file of files) {
        if (file == exclude) continue;
        var m = file.match(/^([0-9]{3})_(.+)\.(pix|axp)$/i);
        if (m) {
            var num = parseInt(m[1]);
            out[num] = file;
        } else {
            out[nn++] = file;
        }
    }

    return out;
}

IgnisProperties.prototype.fileMoveDn = function ()
{
    if ($('.file-record.selected').length == 0) return;
    var fn = $('.file-record.selected').attr('fn');
    if (!fn) return;

    var files = this.getFiles();

    var is_next = false;
    for (var file of files) {
        if (is_next) {
            this.fileMoveUp(file);
            return;
        }
        if (file == fn) {
            is_next = true;
        }
    }
}

IgnisProperties.prototype.getMaxNum = function (files)
{
    var max_num = 0;
    for (var file of files) {
        var m = file.match(/^([0-9]{3})_([a-zA-Z0-9-]+)\.(pix|axp)$/i);
        if (m) {
            var num = parseInt(m[1]);
            if (num > max_num) max_num = num;
        }
    }

    return max_num;
}

IgnisProperties.prototype.getFiles = function ()
{
    var out_files = [];
    var drive = $('#drives').val();
    if (!drive) {
        return;
    }
    try {
        var files = fs.readdirSync(drive).sort();
    } catch (e) {
        return;
    }

    for (var file of files) {
        if (file.match(/\.(pix|axp)$/i)) {
            out_files.push(file);
        }
    }

    return out_files;
}

IgnisProperties.prototype.updateDeviceFiles = function (force_fn)
{
    var fn = $('.file-record.selected').attr('fn');
    if (force_fn) fn = force_fn;

    $('#file-explorer').empty();

    var files = this.getFiles();
    if (!files) return;

    for (var file of files) {
        var el = $('<div class="file-record"></div>');
        el.append($('<div class="file-name"></div>').text(file));
        var btns = $('<div class="file-buttons"></div>');
        //btns.append('<div param="' + file + '" action="file_move_up" class="file-btn file-up">&#11205;</div>');
        //btns.append('<div param="' + file + '" action="file_move_dn" class="file-btn file-dn">&#11206;</div>');
        el.append(btns);
        el.attr('fn', file);
        if (file == fn) el.addClass('selected');
        //el.on('contextmenu', $.proxy(this.fileContextMenu, this));
        el.on('click', $.proxy(this.fileClick, this));
        $('#file-explorer').append(el);
    }
}

IgnisProperties.prototype.deselect = function ()
{
    $('.file-record').removeClass('selected');
}

IgnisProperties.prototype.fileClick = function (e)
{
    $('.file-record').removeClass('selected');
    $(e.delegateTarget).addClass('selected');
    this.ignis.timeline.deselect();
    this.ignis.library.deselect();
}

IgnisProperties.prototype.fileContextMenu = function (e)
{
    $('.delete-popup').remove();
        var del = $('<div class="delete-popup"></div>');
        del.append('<div class="question">Permanently remove file from device?</div>');
        var yes = $('<div class="yes">Yes</div>');
        var no = $('<div class="no">No</div>');
        del.append(yes);
        del.append(no);
        del.css('left', e.pageX + 'px');
        del.css('top', e.pageY + 'px');
        yes.attr('fn', $(e.delegateTarget).attr('fn'));
        $('body').append(del);
        no.on('click', $.proxy(function (e) {
            $('.delete-popup').remove();
        }, this));
        yes.on('click', $.proxy(function (e) {
            this.deleteFile($(e.delegateTarget).attr('fn'));
            $('.delete-popup').remove();
        }, this));
}

IgnisProperties.prototype.fileDelete = function (e, immediate)
{
    if ($('.file-record.selected').length == 0) return;
    var fn = $('.file-record.selected').attr('fn');
    if (!fn) return;

    if (immediate) {
        this.deleteFile(fn);
        return;
    }

    //this.deleteFile(fn);
    $('.delete-popup').remove();
    var del = $('<div class="delete-popup"></div>');
    del.append('<div class="question">Permanently remove file from device?</div>');
    var yes = $('<div class="yes">Yes</div>');
    var no = $('<div class="no">No</div>');
    del.append(yes);
    del.append(no);
    yes.attr('fn', fn);
    $('body').append(del);

    var pos = $('button[action=file_delete]').offset();
    del.css('left', (pos.left - del.width() + 24) + 'px');
    del.css('top', (pos.top + 30) + 'px');
    del.addClass('right');

    no.on('click', $.proxy(function (e) {
        $('.delete-popup').remove();
    }, this));
    yes.on('click', $.proxy(function (e) {
        this.deleteFile($(e.delegateTarget).attr('fn'));
        $('.delete-popup').remove();
    }, this));
}

IgnisProperties.prototype.autoRenameFiles = function ()
{
    var rename = [];
    var files = this.getFiles();
    if (!files) return;
    var drive = $('#drives').val();
    if (!drive) return;
    
    var idx = 1;
    for (var i in files) {
        var fn = files[i];
        var prefix = parseInt(fn.substring(0, 3));
        if (prefix != idx) {
            var nf = (''+idx).padStart(3, '0') + fn.substring(3);
            rename.push({from:drive+fn,to:drive+nf});
        }
        idx++;
    }
    
    for (var f of rename) {
        fs.renameSync(f.from, f.to);
    }

    this.updateDeviceFiles();
}

IgnisProperties.prototype.deleteFile = function (fn)
{
    var drive = $('#drives').val();
    if (!drive) {
        return;
    }

    var fp = drive + fn;
    if (fs.existsSync(fp)) fs.unlinkSync(fp);
    this.autoRenameFiles();
}

IgnisProperties.prototype.projectUpdated = function ()
{
    var project = this.ignis.project;
    $('#project-name').val(project.name);
    if (project.selected_device.length == 0 || $('#leds-count option[value=' + project.selected_device + ']').length == 0) {
        // project is using unknown device
        this.ignis.userconf.set('custom_leds', project.leds);
        project.enable_accelerometer = true;
        project.selected_device = project.leds + '_C';
        this.updateDevices();
    }
    $('#leds-count').val(project.selected_device);
    $('#project-post').val(project.post);
    this.calcMaxFreq();

    if (project.timelinesCount() == 1) {
        $('#ets-label').hide();
        $('#export-timeline-select').hide();
        $('#export-filename').css('width', '100%');
    } else {
        $('#ets-label').show();
        $('#export-timeline-select').show();
        $('#export-filename').css('width', '80%');
    }
}

IgnisProperties.prototype.updatePanel = function ()
{
    var idx = this.ignis.timeline.editor_index;
    var n = this.ignis.project.timeline[idx];

    if (this.isTimelineMultiSelection() || idx === null || idx === undefined || !n) {
        if ($('#properties-image-box').is(':visible')) $('#properties-image-box').hide();
    } else {
        if (!$('#properties-image-box').is(':visible')) $('#properties-image-box').show();
    }

    $('#editor-start-container').show();
    $('#editor-end-container').show();
}

IgnisProperties.prototype.onKeyDown = function (e)
{
    if (e.keyCode == 46 && $('.file-record.selected').length > 0) {
        this.fileDelete(null, true);
    }
    var trg = $('.time-editor').find('[selected]');
    if (trg.length == 0) return;

    if (e.keyCode == 8) {
        this.timeSetKey(trg, 'bckspc');
    }
    if (e.keyCode == 13) {
        this.timeSetKey(trg, 'set');
    }
}

IgnisProperties.prototype.onKey = function (e)
{
    if (e.ctrlKey && e.keyCode == 6) {
        e.preventDefault();
        $('#fit-image-count').val('1');
        this.stretchImage();
        return;
    }

    var trg = $('.time-editor').find('[selected]');
    if (trg.length == 0) return;

    if (e.originalEvent.key == '+') {
        this.timeChange(trg, 1);
    }
    if (e.originalEvent.key == '-') {
        this.timeChange(trg, -1);
    }
    if (e.originalEvent.key.match(/[0-9]/)) {
        this.timeSetKey(trg, e.originalEvent.key);
    }
}

IgnisProperties.prototype.onTimeSelect = function (e)
{
    $('.time-editor').find('.secs,.mins,.millis').attr('selected', false);
    $(e.target).attr('selected', true);
}

IgnisProperties.prototype.onClick = function (e)
{
    if (!$(e.target).parent().hasClass('time-editor')) {
        var trg = $('.time-editor').find('.secs[selected],.mins[selected],.millis[selected]');
        if (trg.length) {
            this.timeSetKey(trg, 'set');
        }
        $('.time-editor').find('.secs,.mins,.millis').attr('selected', false);
    }
}

IgnisProperties.prototype.onTimePlus = function (e)
{
    this.timeChange(e.target, 1);
}

IgnisProperties.prototype.onTimeMinus = function (e)
{
    this.timeChange(e.target, -1);
}

IgnisProperties.prototype.timeSetKey = function (trg, key)
{
    var par = $(trg).parent();
    if (par.find('[selected]').length == 0) {
        $('.time-editor').find('.secs,.mins,.millis').attr('selected', false);
        par.find('.secs').attr('selected', true);
    }
    var f = $(par).attr('for');
    var editor_val = $('[editor='+f+']').val();

    if (key.match(/^[0-9]$/)) {
        var val = $(trg).html();
        val = val + key;
        if (val.length > 3 && $(trg).hasClass('millis')) {
            val = val.substring(val.length - 3, val.length);
        }
        val = parseInt(val);
        if (isNaN(val)) val = 0;
        if ($(trg).hasClass('secs')) {
            if (val > 59) val = parseInt(key);
        }
        $(trg).html(val);
    } else if (key == 'bckspc') {
        var val = $(trg).html();
        val = val.substring(0, val.length - 1);
        val = parseInt(val);
        if (isNaN(val)) val = 0;
        $(trg).html(val);
    } else if (key == 'set') {
        var ms = parseInt(par.find('.millis').html());
        var s = parseInt(par.find('.secs').html());
        var m = parseInt(par.find('.mins').html());
        var new_val = ms + s * 1000 + m * 60000;
    
        $('[editor='+f+']').val(new_val);
        var start = parseInt($('[editor=start]').val());
        var end = parseInt($('[editor=end]').val());
        if (start > end) {
            var t = end;
            end = start;
            start = t;
        }
        var duration = end - start;
        $('[editor=start]').val(start);
        $('[editor=duration]').val(duration);
        $('[editor=end]').val(end);

        this.updateEditor(f);
        this.editorUpdateTimeline();
    }
}

IgnisProperties.prototype.timeChange = function (trg, d)
{
    var par = $(trg).parent();
    if (par.find('[selected]').length == 0) {
        $('.time-editor').find('.secs,.mins,.millis').attr('selected', false);
        par.find('.secs').attr('selected', true);
    }
    var f = $(par).attr('for');

    var val = parseInt($('[editor='+f+']').val());
    
    var nval = 0;
    nval += parseInt($('.time-editor[for='+f+']').find('.secs').html()) * 1000;
    nval += parseInt($('.time-editor[for='+f+']').find('.mins').html()) * 60000;
    nval += parseInt($('.time-editor[for='+f+']').find('.millis').html());

    val = nval;

    var a = 1;
    if (par.find('.secs').attr('selected')) a = 1000;
    if (par.find('.mins').attr('selected')) a = 60000;
    
    val += a * d;
    $('[editor='+f+']').val(val);

    if (f == 'start') {
        var val = parseInt($('[editor=duration]').val());
        val -= a * d;
        $('[editor=duration]').val(val);
    }
    if (f == 'end') {
        var val = parseInt($('[editor=duration]').val());
        val += a * d;
        $('[editor=duration]').val(val);
    }
    if (f == 'duration') {
        var val = parseInt($('[editor=end]').val());
        val += a * d;
        $('[editor=end]').val(val);
    }

    this.updateEditor(f);
    this.editorUpdateTimeline();
}

IgnisProperties.prototype.editorSet = function (i)
{
    const project = this.ignis.project;

    if (!project.timeline[i]) return;

    $('[editor=start]').val(project.timeline[i].start);
    $('[editor=duration]').val(project.timeline[i].duration);
    $('[editor=end]').val(project.timeline[i].end);
    $('[editor=dim]').val(project.timeline[i].dim);
    $('[editor=sep]').val(project.timeline[i].sep);
    $('[editor=gap]').val(project.timeline[i].gap);
    project.timeline[i].frequency = this.clampLineFrequency(project.timeline[i].frequency);
    $('[editor=frequency]').val(project.timeline[i].frequency);
    $('[editor=picture_frequency]').val(project.timeline[i].picture_frequency);
    $('[editor=accelerometer]').prop('checked', project.timeline[i].accelerometer);
    $('[editor=mirror]').prop('checked', project.timeline[i].mirror);
    $('[editor=rotate]').prop('checked', project.timeline[i].rotate);
    $('[editor=reverse]').prop('checked', project.timeline[i].reverse);
    $('[editor=mgap]').val(project.timeline[i].mgap);
    if (this.editor_hue_index !== i) {
        this.resetHuePreview();
        this.editor_hue_index = i;
    }
    this.updateEditor('start');
    this.updateEditor('duration');
    this.updateEditor('end');

    if (project.timeline[i].accelerometer) {
        $('#picture-freq-field').show();
        $('#freq-field').hide();
    } else {
        $('#picture-freq-field').hide();
        $('#freq-field').show();
    }

    if (project.timeline[i].mirror) {
        $('#mgap-slider').show();
        $('#mgap-rotate').show();
        $('#mgap-reverse').show();
    } else {
        $('#mgap-slider').hide();
        $('#mgap-rotate').hide();
        $('#mgap-reverse').hide();
    }
}

IgnisProperties.prototype.exportTechnologyChanged = function ()
{
    if ($('#export-filename').prop('readonly')) {
        $('#export-filename').val(this.ignis.project.getProjectFilename(false, this.ignis.project.getExportExtension()));
    } else {
        $('#export-filename').val(this.ignis.project.sanitizeExportFilename($('#export-filename').val(), this.ignis.project.getExportExtension()));
    }
    this.updateDeviceFiles();
}

IgnisProperties.prototype.getActiveTimelineIndex = function ()
{
    const project = this.ignis.project;
    const timeline = this.ignis.timeline;

    if (this.isTimelineMultiSelection()) return null;

    var active = null;
    if (timeline.selected_nodes && timeline.selected_nodes.length > 0) {
        active = timeline.selected_nodes[0];
    }

    if (active && active.timelineHash == project.currentTimeline) {
        var activeIndex = timeline.findNodeIndexByUid(active.timelineHash, active.uid);
        if (activeIndex >= 0 && project.timeline[activeIndex]) {
            active.index = activeIndex;
            timeline.editor_index = activeIndex;
            return activeIndex;
        }
    }

    var activeEl = $('.timeline-img.active').first();
    if (activeEl.length > 0) {
        var uid = parseInt(activeEl.attr('uid'));
        var domIndex = timeline.findNodeIndexByUid(project.currentTimeline, uid);
        if (domIndex >= 0 && project.timeline[domIndex]) {
            timeline.editor_index = domIndex;
            return domIndex;
        }
    }

    var idx = parseInt(timeline.editor_index);
    if (!isNaN(idx) && project.timeline[idx]) return idx;

    return null;
}

IgnisProperties.prototype.isTimelineMultiSelection = function ()
{
    var timeline = this.ignis.timeline;
    if (timeline.editor_multi) return true;
    if (timeline.selected_nodes && timeline.selected_nodes.length > 1) return true;
    if (timeline.selected_items && timeline.selected_items.length > 1) return true;
    return false;
}

IgnisProperties.prototype.editorUpdateTimeline = function ()
{
    const project = this.ignis.project;

    if (this.isTimelineMultiSelection()) return;

    var i = this.getActiveTimelineIndex();
    if (i === null) return;
    var uid = project.timeline[i].uid;

    project.timeline[i].start = parseInt($('[editor=start]').val());
    if (isNaN(project.timeline[i].start) || project.timeline[i].start < 0) {
        project.timeline[i].start = 0;
        $('[editor=start]').val(0);
    }
    project.timeline[i].end = parseInt($('[editor=end]').val());
    project.timeline[i].duration = project.timeline[i].end - project.timeline[i].start;
    project.timeline[i].gap = parseInt($('[editor=gap]').val());
    project.timeline[i].frequency = this.clampLineFrequency($('[editor=frequency]').val());
    this.rememberLineFrequency(project.timeline[i].frequency);
    project.timeline[i].dim = parseInt($('[editor=dim]').val());
    project.timeline[i].picture_frequency = parseInt($('[editor=picture_frequency]').val());
    project.timeline[i].accelerometer = $('[editor=accelerometer]').is(':checked');
    project.timeline[i].mirror = $('[editor=mirror]').is(':checked');
    project.timeline[i].rotate = $('[editor=rotate]').is(':checked');
    project.timeline[i].reverse = $('[editor=reverse]').is(':checked');
    project.timeline[i].mgap = parseInt($('[editor=mgap]').val());

    this.ignis.preview.setDimIdx(project.getCurrentPreview(), project.timeline[i].dim / 100);

    project.recalculate(i);

    var nextIndex = this.ignis.timeline.findNodeIndexByUid(project.currentTimeline, uid);
    if (nextIndex >= 0) {
        this.ignis.timeline.editor_index = nextIndex;
        i = nextIndex;
    }

    this.editorSet(i);
}

IgnisProperties.prototype.onEditorInputChange = function (e)
{
    var v = $(e.target).val();
    v = parseInt(v);
    if (isNaN(v)) v = 0;

    if ($(e.target).val() != v) $(e.target).val(v);

    this.editorUpdateTimeline();
}

IgnisProperties.prototype.updateEditor = function (f)
{
    var val = parseInt($('[editor='+f+']').val());

    var min = Math.floor(val / 60000);
    val = val - min * 60000;

    var sec = Math.floor(val / 1000);
    val = val - sec * 1000;

    $('.time-editor[for='+f+']').find('.millis').html(val);
    $('.time-editor[for='+f+']').find('.secs').html(sec);
    $('.time-editor[for='+f+']').find('.mins').html(min);
}

IgnisProperties.prototype.updateValue = function (key, value)
{
    console.log('updateValue('+key+','+value+')');
    const project = this.ignis.project;

    if (this.isTimelineMultiSelection()) return;

    var i = this.getActiveTimelineIndex();
    if (i === null || !project.timeline[i]) return;

    if (key == 'frequency') {
        value = this.clampLineFrequency(value);
        $('[editor=frequency]').val(value);
        this.rememberLineFrequency(value);
    }

    project.timeline[i][key] = value;

    if (key == 'dim') {
        this.ignis.preview.setDimIdx(project.getCurrentPreview(), value / 100);
    }
    this.ignis.preview.nodeChanged();
}

IgnisProperties.prototype.getNodeFitWidth = function (node, image)
{
    var ratio = parseFloat(image.resolution.r);
    if (!isFinite(ratio) || ratio <= 0) ratio = 1;
    var leds = parseInt(this.ignis.project.leds);
    if (isNaN(leds) || leds <= 0) leds = config.project.default_leds;

    if (node.mirror) {
        var mirrorGap = parseInt(node.mgap);
        if (isNaN(mirrorGap)) mirrorGap = 0;
        if (mirrorGap % 2 != 0) mirrorGap += 1;
        leds = Math.max(1, Math.floor((leds - mirrorGap) / 2));
    }

    return Math.max(1, Math.round(leds * ratio));
}

IgnisProperties.prototype.getHueValue = function ()
{
    var value = parseInt($('#image-hue-slider').val());
    if (isNaN(value)) value = 0;
    return value;
}

IgnisProperties.prototype.setHueValue = function (value)
{
    if (isNaN(value)) value = 0;
    if (value > 180) value = 180;
    if (value < -180) value = -180;
    $('#image-hue-slider').val(value);
    var slider = $('#image-hue-slider').data('slider');
    if (slider) {
        slider.val = value;
        slider.valUpdate();
    }
    $('#image-hue-value').val(value);
}

IgnisProperties.prototype.onHueValueInput = function ()
{
    var value = parseInt($('#image-hue-value').val());
    if (isNaN(value)) return;
    this.setHueValue(value);
    this.previewHueTransform();
}

IgnisProperties.prototype.applyHueTransform = function ()
{
    var value = this.getHueValue();
    if (value == 0) return;
    this.applyImageTransform({ hue: value });
}

IgnisProperties.prototype.resetHuePreview = function ()
{
    this.setHueValue(0);
    this.clearImageEditPreview();
}

IgnisProperties.prototype.clearImageEditPreview = function ()
{
    $('.timeline-img.image-edit-preview').removeClass('image-edit-preview').css('filter', '');
    $('.library-img.image-edit-preview').removeClass('image-edit-preview').css('filter', '');
}

IgnisProperties.prototype.previewHueTransform = function ()
{
    var value = this.getHueValue();
    this.setHueValue(value);
    this.clearImageEditPreview();

    if (value == 0) return;

    var target = this.getEditableImageTarget();
    if (!target || !target.source) return;

    if (target.type == 'timeline') {
        var node = this.ignis.project.timeline[target.index];
        if (!node) return;
        $('#tlimg-' + node.uid).addClass('image-edit-preview').css('filter', 'hue-rotate(' + value + 'deg)');
        return;
    }

    $('.library-img[hash=' + target.source.hash + ']').addClass('image-edit-preview').css('filter', 'hue-rotate(' + value + 'deg)');
}

IgnisProperties.prototype.getEditableImageTarget = function ()
{
    var idx = this.getActiveTimelineIndex();
    var node = this.ignis.project.timeline[idx];
    if (!this.isTimelineMultiSelection() && idx !== null && idx !== undefined && node) {
        return {
            type: 'timeline',
            index: idx,
            uid: node.uid,
            source: this.ignis.library.getImageByHash(node.hash),
        };
    }

    if (this.ignis.library.selected_item) {
        return {
            type: 'library',
            source: this.ignis.library.selected_item,
        };
    }

    return null;
}

IgnisProperties.prototype.applyImageTransform = function (transform)
{
    var target = this.getEditableImageTarget();
    if (!target || !target.source) {
        alert('Select an image in the timeline or library first.');
        return;
    }

    app_loading(true);
    this.clearImageEditPreview();
    if (target.type == 'library') {
        this.ignis.library.transformImageInPlace(target.source, transform, $.proxy(function (item) {
            app_loading(false);
            if (!item) return;
            this.setHueValue(0);
            this.ignis.library.clearSelected();
            this.ignis.library.selected_item = item;
            this.ignis.library.selected_items = [item.hash];
            $('.library-img[hash=' + item.hash + ']').addClass('selected');
        }, this));
        return;
    }

    transform.generated = true;
    this.ignis.library.transformImage(target.source, transform, $.proxy(function (item) {
        app_loading(false);
        if (!item) return;
        this.setHueValue(0);

        if (target.type == 'timeline') {
            var targetIndex = this.ignis.timeline.findNodeIndexByUid(this.ignis.project.currentTimeline, target.uid);
            if (targetIndex < 0) targetIndex = target.index;
            var node = this.ignis.project.timeline[targetIndex];
            if (!node) return;

            this.ignis.project.historyPush();
            node.hash = item.hash;
            node.path = item.path;
            node.tex_loaded = false;
            this.ignis.project.recalculate(targetIndex);
            targetIndex = this.ignis.timeline.findNodeIndexByUid(this.ignis.project.currentTimeline, target.uid);
            if (targetIndex >= 0) this.ignis.timeline.editor_index = targetIndex;
            $('#tlimg-' + node.uid)
                .css('filter', '')
                .css('background-image', 'url(' + this.ignis.timeline.imageThumbPath(item.hash) + '?t=' + Date.now() + ')');
            this.editorSet(targetIndex >= 0 ? targetIndex : target.index);
            this.ignis.timeline.update();
            this.ignis.preview.nodeChanged();
            app_execute_event('project_updated');
            return;
        }
    }, this));
}

IgnisProperties.prototype.onMouseUp = function (e)
{
    $(e.target).attr('mdown', '0');
}

IgnisProperties.prototype.updateDrives = function ()
{
    $('#drives').empty();

    window.electronApi.listDrives().then((drives) => {
        for (var i in drives) {
            var drive = drives[i];

            //if (drive.busType != 'USB') continue;

            for (var l in drive.mountpoints) {
                var point = drive.mountpoints[l];
                if (point.path == path.sep) continue;
                if (point.path == '/private/var/vm') continue;

                if (point.path[point.path.length-1] != path.sep) point.path += path.sep;

                var el = $('<option></option>');
                el.text(point.path + ' (' + drive.description + ')');
                el.attr('value', point.path);
                $('#drives').append(el);
            }

        }
    });

    setTimeout($.proxy(this.updateDeviceFiles, this), 1000);
}

IgnisProperties.prototype.stretchImage = function ()
{
    if (this.isTimelineMultiSelection()) return;

    var index = this.getActiveTimelineIndex();

    if (index === null || index === undefined) return;

    var n = this.ignis.project.timeline[index];
    if (!n) return;

    var image = this.ignis.library.getImageByHash(n.hash);
    if (!image || !image.resolution) return;
    var rw = this.getNodeFitWidth(n, image);

    var s = parseInt($('[editor=start]').val());
    var e = parseInt($('[editor=end]').val());
    var l = (e - s) / 1000;
    if (!isFinite(l) || l <= 0) return;

    var c = Math.floor(parseFloat($('#fit-image-count').val()));
    if (isNaN(c) || c < 1) {
        c = 1;
    }
    $('#fit-image-count').val(c);

    var lf = this.clampLineFrequency(Math.max(1, Math.floor((rw / l) * c)));

    $('[editor=frequency]').val(lf);

    this.updateValue('frequency', lf);
}
