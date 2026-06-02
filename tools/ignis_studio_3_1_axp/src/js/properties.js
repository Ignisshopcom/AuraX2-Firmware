function IgnisProperties(ignis)
{
    this.ignis = ignis;
    this.lastDrivesCount = 0;
    this.editor_hue_index = null;
    this.exportMode = 'photon';
    this.auraxDevices = [];
    this.selectedAuraXHost = null;
    this.effectPropertyColorIndex = 0;
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

    project.enable_accelerometer = false;
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
    if ($('#leds-count').length == 0) return;

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

    this.updateDevicesData();

    this.updateDrives();

    $('#project-name').val(project.name);
    $('#timeline-name-input').val(project.timelines[project.currentTimeline] ? (project.timelines[project.currentTimeline].name || project.getTimelineDefaultName(project.currentTimeline)) : '');
    $('#leds-count-input').val(project.leds);
    $('#project-post').val(project.post);
    $('#leds-count-input').on('change keyup', $.proxy(function (e) {

        var leds = parseInt($(e.target).val());
        var oleds = $(e.target).val();
        if (isNaN(leds)) leds = config.project.default_leds;
        if (leds < 1) leds = 1;
        if (leds > 9999) leds = 9999;
        this.ignis.project.enable_accelerometer = false;
        this.ignis.project.selected_device = leds + '_C';
        this.ignis.project.setLeds(leds);
        if (e.originalEvent) {
            this.rememberDeviceDefaults();
        }
        if (leds != oleds) {
            $('#leds-count-input').val(leds);
        }
        this.calcMaxFreq();

    }, this));
    $('#project-name').on('change keyup keydown', $.proxy(function (e) {
        this.ignis.project.name = $('#project-name').val();
    }, this));
    $('#timeline-name-input').on('change', $.proxy(function (e) {
        this.ignis.project.renameTimeline(this.ignis.project.currentTimeline, $('#timeline-name-input').val());
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
    app_register_action('export_mode_photon', $.proxy(function () { this.setExportMode('photon'); }, this));
    app_register_action('export_mode_aurax', $.proxy(function () { this.setExportMode('aurax'); }, this));
    app_register_action('aurax_scan', $.proxy(this.scanAuraXDevices, this));
    app_register_action('aurax_upload', $.proxy(this.uploadSelectedAuraX, this));
    $('#aurax-device-list').on('click', '.aurax-identify-btn', $.proxy(this.identifyAuraXDevice, this));
    $('#aurax-device-list').on('click', '.aurax-program-up', $.proxy(this.moveAuraXProgramUp, this));
    $('#aurax-device-list').on('click', '.aurax-program-down', $.proxy(this.moveAuraXProgramDown, this));
    $('#aurax-device-list').on('click', '.aurax-program-delete', $.proxy(this.deleteAuraXProgram, this));
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

    $('#leds-count-input').show();

    setInterval($.proxy(this.autoEnumerate, this), 1000);

    this.calcMaxFreq();

    setInterval($.proxy(this.updateFilenameInput, this), 500);

    $('#export-timeline-select').on('change', $.proxy(this.timelineSelectChanged, this));
    $('#export-technology').on('change', $.proxy(this.exportTechnologyChanged, this));
    $('#aurax-device-list').on('click', '.aurax-device-record', $.proxy(this.selectAuraXDevice, this));

    $('#filename-editable-btn').on('click', $.proxy(this.filenameEditableToggle, this));
    $('#effect-properties-field').on('input', '[data-effect-range]', $.proxy(this.onEffectPropertyRangeInput, this));
    $('#effect-properties-field').on('change mouseup touchend', '[data-effect-range]', $.proxy(this.onEffectPropertyRangeCommit, this));
    $('#effect-properties-field').on('input keyup change', '[data-effect-color]', $.proxy(this.onEffectPropertyColor, this));
    $('#effect-properties-field').on('click', '[data-effect-palette]', $.proxy(this.onEffectPropertyPalette, this));
    $('#effect-properties-field').on('click', '[data-effect-toggle]', $.proxy(this.onEffectPropertyToggle, this));
    if (!this.effectPropertiesCaptureBound) {
        this.effectPropertiesCaptureBound = $.proxy(this.onEffectPropertiesPointerDown, this);
        document.addEventListener('mousedown', this.effectPropertiesCaptureBound, true);
    }

    this.projectUpdated();
    this.setExportMode('photon');
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

    var ext = this.exportMode == 'aurax' ? 'axp' : this.ignis.project.getExportExtension();

    if ($('#export-filename').prop('readonly')) {
        if (this.exportMode == 'aurax') {
            $('#export-filename').val(this.ignis.project.getAuraXUploadFilename());
        } else {
            $('#export-filename').val(this.ignis.project.getProjectFilename(false, ext));
        }
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
    this.setExportMode(this.exportMode || 'photon');
}

IgnisProperties.prototype.setExportMode = function (mode)
{
    this.exportMode = mode == 'aurax' ? 'aurax' : 'photon';
    $('#export-mode-photon').toggleClass('active', this.exportMode == 'photon');
    $('#export-mode-aurax').toggleClass('active', this.exportMode == 'aurax');
    $('#export-photon-panel').toggle(this.exportMode == 'photon');
    $('#export-aurax-panel').toggle(this.exportMode == 'aurax');
    $('#export-technology').val(this.exportMode == 'aurax' ? 'aurax' : 'photon');
    this.updateFilenameInput();
}

IgnisProperties.prototype.setAuraXStatus = function (message, state)
{
    $('#aurax-export-status')
        .removeClass('success error busy')
        .addClass(state || '')
        .text(message || '');
}

IgnisProperties.prototype.formatAuraXBytes = function (bytes)
{
    bytes = Number(bytes) || 0;
    if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
    if (bytes >= 1024) return Math.round(bytes / 1024) + ' KB';
    return bytes + ' B';
}

IgnisProperties.prototype.selectAuraXDevice = function (e)
{
    this.selectedAuraXHost = $(e.currentTarget).attr('data-host');
    this.renderAuraXDevices();
}

IgnisProperties.prototype.getSelectedAuraXDevice = function ()
{
    for (var i = 0; i < this.auraxDevices.length; i++) {
        var device = this.auraxDevices[i];
        if ((device.host || device.ip) == this.selectedAuraXHost) return device;
    }
    return null;
}

IgnisProperties.prototype.renderAuraXDevices = function ()
{
    var list = $('#aurax-device-list');
    list.empty();

    if (!this.auraxDevices || this.auraxDevices.length == 0) {
        list.append($('<div>').addClass('aurax-empty').text('No AuraX devices found.'));
        return;
    }

    if (!this.selectedAuraXHost) {
        this.selectedAuraXHost = this.auraxDevices[0].host || this.auraxDevices[0].ip;
    }

    for (var i = 0; i < this.auraxDevices.length; i++) {
        var device = this.auraxDevices[i];
        var host = device.host || device.ip;
        var selected = host == this.selectedAuraXHost;
        var record = $('<div>')
            .addClass('aurax-device-record')
            .toggleClass('selected', selected)
            .attr('data-host', host);

        var main = $('<div>');
        main.append($('<strong>').text(device.deviceName || device.hostname || 'AuraX'));
        main.append($('<span>').text(device.ip || host));

        record.append(main);
        var actions = $('<div>').addClass('aurax-device-actions');
        actions.append($('<span>').addClass('aurax-device-leds').text((device.numLeds || '?') + ' LEDs'));
        actions.append($('<button>')
            .attr('type', 'button')
            .attr('title', 'Blink this device')
            .attr('data-host', host)
            .addClass('aurax-identify-btn')
            .html('<i class="fas fa-bolt"></i>'));
        record.append(actions);

        var details = $('<div>').addClass('aurax-device-details');
        details.append($('<span>').html('<i class="fas fa-wifi"></i>' + (device.wifiPct !== null && device.wifiPct !== undefined ? device.wifiPct + '%' : '-')));
        details.append($('<span>').html('<i class="fas fa-battery-half"></i>' + (device.batteryPct !== null && device.batteryPct !== undefined ? device.batteryPct + '%' : '-')));
        details.append($('<span>').html('<i class="fas fa-hdd"></i>' + this.formatAuraXBytes(device.fsFree || 0) + ' free'));
        record.append(details);

        if (selected) {
            var programs = $('<div>').addClass('aurax-programs');
            var files = device.programs || [];
            if (files.length == 0) {
                programs.append($('<div>').addClass('aurax-program-empty').text('No programs on this device.'));
            }
            for (var p = 0; p < files.length; p++) {
                var file = files[p];
                var row = $('<div>').addClass('aurax-program-row');
                row.append($('<span>').addClass('aurax-program-slot').text(file.slot || (p + 1)));
                var name = $('<div>').addClass('aurax-program-name');
                name.append($('<strong>').text(file.display_name || file.name || 'program'));
                name.append($('<span>').text(this.formatAuraXBytes(file.size || 0)));
                row.append(name);
                var buttons = $('<div>').addClass('aurax-program-actions');
                buttons.append($('<button>')
                    .attr('type', 'button')
                    .attr('title', 'Move up')
                    .attr('data-host', host)
                    .attr('data-slot', file.slot || (p + 1))
                    .prop('disabled', p == 0)
                    .addClass('aurax-program-up')
                    .html('<i class="fas fa-angle-up"></i>'));
                buttons.append($('<button>')
                    .attr('type', 'button')
                    .attr('title', 'Move down')
                    .attr('data-host', host)
                    .attr('data-slot', file.slot || (p + 1))
                    .prop('disabled', p == files.length - 1)
                    .addClass('aurax-program-down')
                    .html('<i class="fas fa-angle-down"></i>'));
                buttons.append($('<button>')
                    .attr('type', 'button')
                    .attr('title', 'Delete program')
                    .attr('data-host', host)
                    .attr('data-file', file.name || '')
                    .addClass('aurax-program-delete')
                    .html('<i class="fas fa-trash-alt"></i>'));
                row.append(buttons);
                programs.append(row);
            }
            record.append(programs);
        }
        list.append(record);
    }
}

IgnisProperties.prototype.updateAuraXDevice = function (host, device)
{
    if (!device) return;
    var normalized = device.host || device.ip || host;
    for (var i = 0; i < this.auraxDevices.length; i++) {
        var currentHost = this.auraxDevices[i].host || this.auraxDevices[i].ip;
        if (currentHost == host || currentHost == normalized) {
            this.auraxDevices[i] = device;
            this.selectedAuraXHost = device.host || device.ip || host;
            this.renderAuraXDevices();
            return;
        }
    }
    this.auraxDevices.push(device);
    this.selectedAuraXHost = device.host || device.ip || host;
    this.renderAuraXDevices();
}

IgnisProperties.prototype.refreshAuraXDevice = function (host)
{
    var api = window.ignisElectron || window.electronApi || {};
    if (!api.refreshAuraXDevice) return Promise.resolve(null);
    return api.refreshAuraXDevice(host, 1200).then($.proxy(function (device) {
        this.updateAuraXDevice(host, device);
        return device;
    }, this));
}

IgnisProperties.prototype.identifyAuraXDevice = function (e)
{
    e.preventDefault();
    e.stopPropagation();
    var api = window.ignisElectron || window.electronApi || {};
    var host = $(e.currentTarget).attr('data-host');
    if (!api.identifyAuraXDevice) {
        this.setAuraXStatus('Device blink is not available in this Ignis Studio build.', 'error');
        return;
    }
    this.setAuraXStatus('Blinking selected AuraX device...', 'busy');
    api.identifyAuraXDevice(host).then($.proxy(function (res) {
        this.setAuraXStatus(res && res.ok ? 'Device blink sent.' : ((res && res.body) || 'Device blink failed.'), res && res.ok ? 'success' : 'error');
    }, this)).catch($.proxy(function (err) {
        this.setAuraXStatus(err && err.message ? err.message : 'Device blink failed.', 'error');
    }, this));
}

IgnisProperties.prototype.moveAuraXProgram = function (e, direction)
{
    e.preventDefault();
    e.stopPropagation();
    var api = window.ignisElectron || window.electronApi || {};
    var host = $(e.currentTarget).attr('data-host');
    var slot = parseInt($(e.currentTarget).attr('data-slot'));
    if (!api.reorderAuraXProgram) {
        this.setAuraXStatus('AuraX program reorder is not available in this Ignis Studio build.', 'error');
        return;
    }
    this.setAuraXStatus('Reordering AuraX programs...', 'busy');
    api.reorderAuraXProgram(host, slot, direction).then($.proxy(function (res) {
        if (!res || !res.ok) {
            this.setAuraXStatus((res && res.body) || 'AuraX reorder failed.', 'error');
            return;
        }
        this.setAuraXStatus('AuraX program order updated.', 'success');
        this.refreshAuraXDevice(host);
    }, this)).catch($.proxy(function (err) {
        this.setAuraXStatus(err && err.message ? err.message : 'AuraX reorder failed.', 'error');
    }, this));
}

IgnisProperties.prototype.moveAuraXProgramUp = function (e)
{
    this.moveAuraXProgram(e, -1);
}

IgnisProperties.prototype.moveAuraXProgramDown = function (e)
{
    this.moveAuraXProgram(e, 1);
}

IgnisProperties.prototype.deleteAuraXProgram = function (e)
{
    e.preventDefault();
    e.stopPropagation();
    var api = window.ignisElectron || window.electronApi || {};
    var host = $(e.currentTarget).attr('data-host');
    var file = $(e.currentTarget).attr('data-file');
    if (!api.deleteAuraXProgram) {
        this.setAuraXStatus('AuraX program delete is not available in this Ignis Studio build.', 'error');
        return;
    }
    if (!confirm('Delete this program from the AuraX device?')) return;
    this.setAuraXStatus('Deleting AuraX program...', 'busy');
    api.deleteAuraXProgram(host, file).then($.proxy(function (res) {
        if (!res || !res.ok) {
            this.setAuraXStatus((res && res.body) || 'AuraX delete failed.', 'error');
            return;
        }
        this.setAuraXStatus('AuraX program deleted.', 'success');
        this.refreshAuraXDevice(host);
    }, this)).catch($.proxy(function (err) {
        this.setAuraXStatus(err && err.message ? err.message : 'AuraX delete failed.', 'error');
    }, this));
}

IgnisProperties.prototype.scanAuraXDevices = function ()
{
    var api = window.ignisElectron || window.electronApi || {};
    if (!api.scanAuraXDevices) {
        this.setAuraXStatus('AuraX scan is not available in this Ignis Studio build.', 'error');
        return;
    }

    this.setAuraXStatus('Scanning local network...', 'busy');
    $('[action=aurax_scan]').prop('disabled', true);

    api.scanAuraXDevices(2600).then($.proxy(function (devices) {
        this.auraxDevices = devices || [];
        if (this.auraxDevices.length > 0) {
            var stillSelected = false;
            for (var i = 0; i < this.auraxDevices.length; i++) {
                if ((this.auraxDevices[i].host || this.auraxDevices[i].ip) == this.selectedAuraXHost) stillSelected = true;
            }
            if (!stillSelected) this.selectedAuraXHost = this.auraxDevices[0].host || this.auraxDevices[0].ip;
            this.setAuraXStatus('Found ' + this.auraxDevices.length + ' AuraX device' + (this.auraxDevices.length == 1 ? '.' : 's.'), 'success');
        } else {
            this.selectedAuraXHost = null;
            this.setAuraXStatus('No AuraX devices found. Make sure the PC and device are on the same network.', 'error');
        }
        this.renderAuraXDevices();
    }, this)).catch($.proxy(function (e) {
        this.auraxDevices = [];
        this.selectedAuraXHost = null;
        this.renderAuraXDevices();
        this.setAuraXStatus(e && e.message ? e.message : 'AuraX scan failed.', 'error');
    }, this)).finally(function () {
        $('[action=aurax_scan]').prop('disabled', false);
    });
}

IgnisProperties.prototype.uploadSelectedAuraX = function ()
{
    var api = window.ignisElectron || window.electronApi || {};
    if (!api.uploadAuraXProgram) {
        this.setAuraXStatus('AuraX upload is not available in this Ignis Studio build.', 'error');
        return;
    }

    var device = this.getSelectedAuraXDevice();
    if (!device) {
        this.setAuraXStatus('Select an AuraX device first.', 'error');
        return;
    }

    var projectLeds = parseInt(this.ignis.project.leds);
    var deviceLeds = parseInt(device.numLeds);
    if (deviceLeds > 0 && projectLeds != deviceLeds) {
        this.setAuraXStatus('Program has ' + projectLeds + ' LEDs, but this device is set to ' + deviceLeds + '.', 'error');
        return;
    }

    app_loading(true);
    $('[action=aurax_upload]').prop('disabled', true);
    this.setAuraXStatus('Building AuraX program...', 'busy');

    this.ignis.project.buildAuraXExport($.proxy(function (err, result) {
        if (err) {
            app_loading(false);
            $('[action=aurax_upload]').prop('disabled', false);
            this.setAuraXStatus(err.message || 'AuraX export failed.', 'error');
            return;
        }

        var bytes = result.length || result.byteLength || 0;
        if (device.fsFree && bytes > device.fsFree) {
            app_loading(false);
            $('[action=aurax_upload]').prop('disabled', false);
            this.setAuraXStatus('Program is ' + this.formatAuraXBytes(bytes) + ', but device has only ' + this.formatAuraXBytes(device.fsFree) + ' free.', 'error');
            return;
        }

        var filename = this.ignis.project.getAuraXUploadFilename();
        this.setAuraXStatus('Uploading ' + filename + ' (' + this.formatAuraXBytes(bytes) + ')...', 'busy');

        api.uploadAuraXProgram(device.host || device.ip, filename, result).then($.proxy(function (res) {
            if (res && res.ok) {
                this.setAuraXStatus(res.body || 'OK: uploaded ' + filename, 'success');
                device.fsFree = Math.max(0, (device.fsFree || 0) - bytes);
                this.renderAuraXDevices();
                this.refreshAuraXDevice(device.host || device.ip);
            } else {
                this.setAuraXStatus((res && res.body) || 'AuraX upload failed.', 'error');
            }
        }, this)).catch($.proxy(function (e) {
            this.setAuraXStatus(e && e.message ? e.message : 'AuraX upload failed.', 'error');
        }, this)).finally(function () {
            app_loading(false);
            $('[action=aurax_upload]').prop('disabled', false);
        });
    }, this));
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
    $('#timeline-name-input').val(project.timelines[project.currentTimeline] ? (project.timelines[project.currentTimeline].name || project.getTimelineDefaultName(project.currentTimeline)) : '');
    $('#leds-count-input').val(project.leds);
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
    var isEffect = project.timeline[i].type == 'effect';
    project.timeline[i].accelerometer = false;

    $('[editor=start]').val(project.timeline[i].start);
    $('[editor=duration]').val(project.timeline[i].duration);
    $('[editor=end]').val(project.timeline[i].end);
    $('[editor=dim]').val(project.timeline[i].dim);
    $('[editor=sep]').val(project.timeline[i].sep);
    $('[editor=gap]').val(project.timeline[i].gap);
    project.timeline[i].frequency = this.clampLineFrequency(project.timeline[i].frequency);
    $('[editor=frequency]').val(project.timeline[i].frequency);
    $('[editor=picture_frequency]').val(project.timeline[i].picture_frequency);
    $('[editor=mirror]').prop('checked', project.timeline[i].mirror);
    $('[editor=rotate]').prop('checked', project.timeline[i].rotate);
    $('[editor=reverse]').prop('checked', project.timeline[i].reverse);
    $('[editor=mgap]').val(project.timeline[i].mgap);
    $('#properties-image-box .properties-title').text(isEffect ? 'Effect' : 'Image');
    $('.image-edit-field').toggle(!isEffect);
    $('#effect-properties-field').toggle(isEffect);
    $('#fit-image-count').closest('.field').toggle(!isEffect);
    $('[editor=gap]').closest('.field').toggle(!isEffect);
    $('#freq-field').toggle(!isEffect);
    $('#mirror-checkbox').closest('.field').show();
    if (isEffect) {
        this.renderEffectProperties(project.timeline[i]);
    } else {
        $('#effect-properties-field').empty();
    }
    if (this.editor_hue_index !== i) {
        this.resetHuePreview();
        this.editor_hue_index = i;
    }
    this.updateEditor('start');
    this.updateEditor('duration');
    this.updateEditor('end');

    $('#picture-freq-field').hide();
    $('#freq-field').toggle(!isEffect);

    if (project.timeline[i].mirror) {
        $('#mgap-slider').show();
        $('#mgap-rotate').show();
    } else {
        $('#mgap-slider').hide();
        $('#mgap-rotate').hide();
    }
    $('#mgap-reverse').toggle(isEffect || project.timeline[i].mirror);
}

IgnisProperties.prototype.renderEffectProperties = function (node)
{
    var box = $('#effect-properties-field');
    box.empty();
    if (!node || node.type != 'effect') return;
    box.attr('data-effect-uid', node.uid);
    box.show();

    var self = this;
    var uid = node.uid;
    var library = this.ignis.library;
    var effect = library.getEffectById(node.effectId);
    var slots = library.getEffectColorSlots(effect.id);
    node.effectSpeed = library.clampEffectNumber(node.effectSpeed, 10, 1000);
    node.effectIntensity = library.clampEffectNumber(node.effectIntensity, 0, 255);
    node.effectSize = library.clampEffectNumber(node.effectSize, 1, effect.sizeMax || 40);
    node.effectColors = (node.effectColors && node.effectColors.length ? node.effectColors : effect.colors || ['#ff6000', '#00b4ff', '#ffffff']).slice(0);

    box.append($('<label class="effect-property-name"></label>').html('<i class="fas fa-magic"></i> ' + effect.name));

    if (effect.speed) box.append(this.createEffectPropertyRange(effect.speed, 'effectSpeed', node.effectSpeed, 10, 1000, uid));
    if (effect.intensity) box.append(this.createEffectPropertyRange(effect.intensity, 'effectIntensity', node.effectIntensity, 0, 255, uid));
    if (effect.size) box.append(this.createEffectPropertyRange(effect.size, 'effectSize', node.effectSize, 1, effect.sizeMax || 40, uid));

    if (slots > 0) {
        var colors = $('<div class="effect-property-colors"></div>');
        if (this.effectPropertyColorIndex >= slots) this.effectPropertyColorIndex = 0;
        for (var c = 0; c < slots; c++) {
            var color = library.normalizeEffectColor(node.effectColors[c] || '#000000');
            var row = $('<label></label>');
            row.append($('<span></span>').text('Color ' + (c + 1)));
            row.append($('<button type="button" class="effect-color-current"></button>')
                .attr('data-effect-color-current', c)
                .attr('data-effect-uid', uid)
                .toggleClass('active', c == this.effectPropertyColorIndex)
                .css('background-color', color)
                .attr('title', color));
            row.append($('<b></b>').text(color));
            colors.append(row);
        }
        var wheel = $('<div class="effect-property-wheel-wrap"></div>');
        wheel.append($('<canvas class="effect-property-color-wheel" width="180" height="180"></canvas>').attr('data-effect-uid', uid));
        wheel.append($('<div class="effect-property-wheel-cursor"></div>'));
        colors.append(wheel);
        colors.append(this.createEffectPropertyValueRange(uid, node));
        box.append(colors);
        this.drawEffectPropertyColorWheel();
        this.refreshEffectPropertyColorCursor(node);
    }

    var palettes = $('<div class="effect-property-palettes"></div>');
    for (var p in library.effectPalettes) {
        var pal = library.effectPalettes[p];
        var btn = $('<button type="button"></button>');
        btn.attr('data-effect-palette', pal.id);
        btn.toggleClass('active', parseInt(node.effectPaletteId || 0) == pal.id);
        btn.append($('<span></span>').text(pal.name));
        btn.append($('<i></i>').css('background', 'linear-gradient(90deg,' + pal.colors.join(',') + ')'));
        btn.on('click', (function (paletteId) {
            return function (e) {
                e.preventDefault();
                e.stopPropagation();
                self.setEffectNodePalette(uid, paletteId);
            };
        })(pal.id));
        palettes.append(btn);
    }
    box.append(palettes);

    var transforms = $('<div class="effect-transform-actions"></div>');
    transforms.append(this.createEffectToggleButton('effectRotate180', 'fa-redo', 'Rotate'));
    transforms.append(this.createEffectToggleButton('effectFlipH', 'fa-arrows-alt-h', 'Flip H'));
    transforms.append(this.createEffectToggleButton('effectFlipV', 'fa-arrows-alt-v', 'Flip V'));
    transforms.find('[data-effect-toggle]').each(function () {
        var key = $(this).attr('data-effect-toggle');
        $(this).toggleClass('active', !!node[key]);
        $(this).on('click', function (e) {
            e.preventDefault();
            e.stopPropagation();
            self.toggleEffectNodeProperty(uid, key);
        });
    });
    box.append(transforms);
}

IgnisProperties.prototype.createEffectPropertyRange = function (label, key, value, min, max, uid)
{
    var row = $('<label class="effect-property-row"></label>');
    row.append($('<span></span>').text(label));
    var input = $('<input type="range">').attr({ min: min, max: max, step: 1, value: value, 'data-effect-range': key, 'data-effect-uid': uid });
    row.append(input);
    row.append($('<b></b>').text(value));
    input.on('input', $.proxy(this.onEffectPropertyRangeInput, this));
    input.on('change mouseup touchend', $.proxy(this.onEffectPropertyRangeCommit, this));
    return row;
}

IgnisProperties.prototype.createEffectPropertyValueRange = function (uid, node)
{
    var color = (node.effectColors || [])[this.effectPropertyColorIndex] || '#ffffff';
    var hsv = this.ignis.library.effectRgbToHsv(this.ignis.library.effectHexToRgb(color));
    var value = Math.round(hsv.v * 100);
    var row = $('<label class="effect-property-row effect-property-value-row"></label>');
    row.append($('<span></span>').text('Density'));
    var input = $('<input type="range">').attr({ min: 0, max: 100, step: 1, value: value, 'data-effect-value': 'density', 'data-effect-uid': uid });
    row.append(input);
    row.append($('<b></b>').text(value + '%'));
    input.on('input', $.proxy(this.onEffectPropertyValueInput, this));
    input.on('change mouseup touchend', $.proxy(this.onEffectPropertyValueCommit, this));
    return row;
}

IgnisProperties.prototype.createEffectToggleButton = function (key, icon, label)
{
    return $('<button type="button"></button>')
        .attr('data-effect-toggle', key)
        .html('<i class="fas ' + icon + '"></i><span>' + label + '</span>');
}

IgnisProperties.prototype.onEffectPropertiesPointerDown = function (e)
{
    var target = e.target;
    var current = $(target).closest('[data-effect-color-current]');
    if (current.length) {
        e.preventDefault();
        e.stopPropagation();
        if (e.stopImmediatePropagation) e.stopImmediatePropagation();
        this.effectPropertyColorIndex = parseInt(current.attr('data-effect-color-current')) || 0;
        var node = this.getEffectNodeByUid(current.attr('data-effect-uid'));
        if (node) {
            $('#effect-properties-field [data-effect-color-current]').removeClass('active');
            current.addClass('active');
            this.refreshEffectPropertyColorCursor(node);
        }
        return false;
    }

    var range = $(target).closest('#effect-properties-field [data-effect-range], #effect-properties-field [data-effect-value]');
    if (range.length) {
        return true;
    }

    var wheel = $(target).closest('#effect-properties-field canvas.effect-property-color-wheel');
    if (wheel.length) {
        e.preventDefault();
        e.stopPropagation();
        if (e.stopImmediatePropagation) e.stopImmediatePropagation();
        this.beginEffectPropertyColorWheel(e, wheel.attr('data-effect-uid'));
        return false;
    }

    var palette = $(target).closest('#effect-properties-field [data-effect-palette]');
    if (palette.length) {
        e.preventDefault();
        e.stopPropagation();
        if (e.stopImmediatePropagation) e.stopImmediatePropagation();
        this.setEffectNodePalette($('#effect-properties-field').attr('data-effect-uid'), palette.attr('data-effect-palette'));
        return false;
    }

    var toggle = $(target).closest('#effect-properties-field [data-effect-toggle]');
    if (toggle.length) {
        e.preventDefault();
        e.stopPropagation();
        if (e.stopImmediatePropagation) e.stopImmediatePropagation();
        this.toggleEffectNodeProperty($('#effect-properties-field').attr('data-effect-uid'), toggle.attr('data-effect-toggle'));
        return false;
    }
}

IgnisProperties.prototype.drawEffectPropertyColorWheel = function ()
{
    var canvas = $('#effect-properties-field canvas.effect-property-color-wheel')[0];
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
            var rgb = this.ignis.library.effectHexToRgb(this.ignis.library.effectHsvToHex({ h: hue, s: sat, v: 1 }));
            image.data[idx] = rgb.r;
            image.data[idx + 1] = rgb.g;
            image.data[idx + 2] = rgb.b;
            image.data[idx + 3] = 255;
        }
    }
    ctx.putImageData(image, 0, 0);
    canvas._ignisColorWheelDrawn = true;
}

IgnisProperties.prototype.getEffectPropertyWheelPoint = function (e, rect)
{
    return {
        x: e.clientX - rect.left,
        y: e.clientY - rect.top
    };
}

IgnisProperties.prototype.beginEffectPropertyColorWheel = function (e, uid)
{
    var move = $.proxy(function (ev) {
        this.moveEffectPropertyColorWheel(ev, uid);
    }, this);
    var up = $.proxy(function (ev) {
        document.removeEventListener('mousemove', move, true);
        document.removeEventListener('mouseup', up, true);
        this.moveEffectPropertyColorWheel(ev, uid, true);
    }, this);
    document.addEventListener('mousemove', move, true);
    document.addEventListener('mouseup', up, true);
    this.moveEffectPropertyColorWheel(e, uid);
}

IgnisProperties.prototype.moveEffectPropertyColorWheel = function (e, uid, final)
{
    if (e && e.preventDefault) e.preventDefault();
    if (e && e.stopPropagation) e.stopPropagation();
    if (e && e.stopImmediatePropagation) e.stopImmediatePropagation();
    var canvas = $('#effect-properties-field canvas.effect-property-color-wheel')[0];
    var node = this.getEffectNodeByUid(uid);
    if (!canvas || !node) return;
    var rect = canvas.getBoundingClientRect();
    var point = this.getEffectPropertyWheelPoint(e, rect);
    var cx = rect.width / 2;
    var cy = rect.height / 2;
    var dx = point.x - cx;
    var dy = point.y - cy;
    var radius = Math.max(1, Math.min(cx, cy));
    var sat = Math.min(1, Math.sqrt(dx * dx + dy * dy) / radius);
    var hue = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360;
    var colors = (node.effectColors || []).slice(0);
    var current = colors[this.effectPropertyColorIndex] || '#ffffff';
    var hsv = this.ignis.library.effectRgbToHsv(this.ignis.library.effectHexToRgb(current));
    hsv.h = hue;
    hsv.s = sat;
    var color = this.ignis.library.effectHsvToHex(hsv);
    this.setEffectNodeColor(uid, this.effectPropertyColorIndex, color, final ? 'rerender' : 'light');
    this.refreshEffectPropertyColorCursor(this.getEffectNodeByUid(uid));
}

IgnisProperties.prototype.refreshEffectPropertyColorCursor = function (node)
{
    if (!node) return;
    var color = (node.effectColors || [])[this.effectPropertyColorIndex] || '#ffffff';
    var hsv = this.ignis.library.effectRgbToHsv(this.ignis.library.effectHexToRgb(color));
    var wheel = $('#effect-properties-field canvas.effect-property-color-wheel');
    var cursor = $('#effect-properties-field .effect-property-wheel-cursor');
    if (!wheel.length || !cursor.length) return;
    var rect = wheel[0].getBoundingClientRect();
    var radius = Math.min(rect.width, rect.height) / 2;
    var x = rect.width / 2 + Math.cos(hsv.h * Math.PI / 180) * hsv.s * radius;
    var y = rect.height / 2 + Math.sin(hsv.h * Math.PI / 180) * hsv.s * radius;
    cursor.css({ left: x + 'px', top: y + 'px', background: color });
    $('#effect-properties-field [data-effect-value]').val(Math.round(hsv.v * 100));
    $('#effect-properties-field [data-effect-value]').siblings('b').text(Math.round(hsv.v * 100) + '%');
}

IgnisProperties.prototype.getSelectedEffectNode = function ()
{
    var uid = parseInt($('#effect-properties-field').attr('data-effect-uid'));
    if (!isNaN(uid)) {
        for (var i = 0; i < this.ignis.project.timeline.length; i++) {
            var n = this.ignis.project.timeline[i];
            if (n && n.uid == uid && n.type == 'effect') return n;
        }
    }

    var idx = this.getActiveTimelineIndex();
    if (idx === null || idx === undefined) return null;
    var node = this.ignis.project.timeline[idx];
    return (node && node.type == 'effect') ? node : null;
}

IgnisProperties.prototype.getEffectNodeByUid = function (uid)
{
    uid = parseInt(uid);
    if (isNaN(uid)) return null;
    for (var i = 0; i < this.ignis.project.timeline.length; i++) {
        var node = this.ignis.project.timeline[i];
        if (node && node.uid == uid && node.type == 'effect') return node;
    }
    return null;
}

IgnisProperties.prototype.updateSelectedEffectNode = function (patch, mode)
{
    var node = this.getSelectedEffectNode();
    if (!node) return;
    this.updateEffectNode(node, patch, mode);
}

IgnisProperties.prototype.updateEffectNodeByUid = function (uid, patch, mode)
{
    var node = this.getEffectNodeByUid(uid);
    if (!node) return;
    this.updateEffectNode(node, patch, mode);
}

IgnisProperties.prototype.updateEffectNode = function (node, patch, mode)
{
    $.extend(node, patch || {});
    if (mode == 'light') {
        this.refreshSelectedEffectPreview(node, false);
        return;
    }
    this.refreshSelectedEffectPreview(node, true);
    if (mode == 'rerender') this.renderEffectProperties(node);
}

IgnisProperties.prototype.setEffectNodeColor = function (uid, index, raw, mode)
{
    var node = this.getEffectNodeByUid(uid);
    if (!node) return;
    var colors = (node.effectColors || []).slice(0);
    raw = String(raw || '').trim();
    if (raw.charAt(0) != '#') raw = '#' + raw;
    if (!raw.match(/^#[0-9a-fA-F]{6}$/)) raw = colors[index] || '#ffffff';
    colors[index] = this.ignis.library.normalizeEffectColor(raw);
    this.updateEffectNode(node, { effectColors: colors, effectPaletteId: 0 }, mode || 'full');
    $('#effect-properties-field [data-effect-palette]').removeClass('active');
    $('#effect-properties-field [data-effect-palette="0"]').addClass('active');
    $('#effect-properties-field [data-effect-color-current="' + index + '"]').css('background-color', colors[index]).attr('title', colors[index]);
    $('#effect-properties-field [data-effect-color-current="' + index + '"]').siblings('b').text(colors[index]);
}

IgnisProperties.prototype.setEffectNodeColorValue = function (uid, value, mode)
{
    var node = this.getEffectNodeByUid(uid);
    if (!node) return;
    value = parseInt(value);
    if (isNaN(value)) value = 100;
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    var colors = (node.effectColors || []).slice(0);
    var current = colors[this.effectPropertyColorIndex] || '#ffffff';
    var hsv = this.ignis.library.effectRgbToHsv(this.ignis.library.effectHexToRgb(current));
    hsv.v = value / 100;
    this.setEffectNodeColor(uid, this.effectPropertyColorIndex, this.ignis.library.effectHsvToHex(hsv), mode || 'light');
    this.refreshEffectPropertyColorCursor(this.getEffectNodeByUid(uid));
}

IgnisProperties.prototype.setEffectNodePalette = function (uid, paletteId)
{
    var palette = this.ignis.library.getEffectPalette(paletteId);
    this.updateEffectNodeByUid(uid, { effectPaletteId: palette.id, effectColors: palette.colors.slice(0) }, 'rerender');
}

IgnisProperties.prototype.toggleEffectNodeProperty = function (uid, key)
{
    var node = this.getEffectNodeByUid(uid);
    if (!node) return;
    var patch = {};
    patch[key] = !node[key];
    this.updateEffectNode(node, patch, 'rerender');
}

IgnisProperties.prototype.refreshSelectedEffectPreview = function (node, full)
{
    var timeline = this.ignis.timeline;
    if (!timeline || !node) return;
    var el = $('#tlimg-' + node.uid);
    if (el.length && timeline.updateNodeElementImage) {
        el.attr('display-image', '');
        timeline.updateNodeElementImage(el, node, this.ignis.project.leds);
    }
    if (full && this.ignis.library) {
        this.ignis.library.refreshEffectTimeline();
    } else if (this.ignis.preview) {
        this.ignis.preview.nodeChanged();
    }
}

IgnisProperties.prototype.onEffectPropertyRangeInput = function (e)
{
    if (e && e.stopPropagation) e.stopPropagation();
    var key = $(e.currentTarget).attr('data-effect-range');
    var uid = $(e.currentTarget).attr('data-effect-uid') || $('#effect-properties-field').attr('data-effect-uid');
    var val = parseInt($(e.currentTarget).val());
    if (isNaN(val)) val = 0;
    $(e.currentTarget).siblings('b').text(val);
    var patch = {};
    patch[key] = val;
    this.updateEffectNodeByUid(uid, patch, 'light');
}

IgnisProperties.prototype.onEffectPropertyRangeCommit = function (e)
{
    this.onEffectPropertyRangeInput(e);
    var uid = $(e.currentTarget).attr('data-effect-uid') || $('#effect-properties-field').attr('data-effect-uid');
    var node = this.getEffectNodeByUid(uid);
    if (node) this.refreshSelectedEffectPreview(node, true);
}

IgnisProperties.prototype.onEffectPropertyValueInput = function (e)
{
    if (e && e.stopPropagation) e.stopPropagation();
    var value = parseInt($(e.currentTarget).val());
    if (isNaN(value)) value = 100;
    $(e.currentTarget).siblings('b').text(value + '%');
    this.setEffectNodeColorValue($(e.currentTarget).attr('data-effect-uid'), value, 'light');
}

IgnisProperties.prototype.onEffectPropertyValueCommit = function (e)
{
    this.onEffectPropertyValueInput(e);
    var node = this.getEffectNodeByUid($(e.currentTarget).attr('data-effect-uid'));
    if (node) this.refreshSelectedEffectPreview(node, true);
}

IgnisProperties.prototype.onEffectPropertyColor = function (e)
{
    var node = this.getSelectedEffectNode();
    if (!node) return;
    var idx = parseInt($(e.currentTarget).attr('data-effect-color'));
    var colors = (node.effectColors || []).slice(0);
    var raw = String($(e.currentTarget).val() || '').trim();
    if (raw.charAt(0) != '#') raw = '#' + raw;
    if (!raw.match(/^#[0-9a-fA-F]{6}$/)) raw = colors[idx] || '#ffffff';
    colors[idx] = this.ignis.library.normalizeEffectColor(raw);
    $(e.currentTarget).val(colors[idx]);
    $(e.currentTarget).siblings('i').css('background-color', colors[idx]);
    this.updateSelectedEffectNode({ effectColors: colors, effectPaletteId: 0 }, e.type == 'input' ? 'light' : 'full');
    $('#effect-properties-field [data-effect-palette]').removeClass('active');
    $('#effect-properties-field [data-effect-palette="0"]').addClass('active');
}

IgnisProperties.prototype.onEffectPropertyPalette = function (e)
{
    var palette = this.ignis.library.getEffectPalette($(e.currentTarget).attr('data-effect-palette'));
    this.updateSelectedEffectNode({ effectPaletteId: palette.id, effectColors: palette.colors.slice(0) }, 'rerender');
}

IgnisProperties.prototype.onEffectPropertyToggle = function (e)
{
    var node = this.getSelectedEffectNode();
    if (!node) return;
    var key = $(e.currentTarget).attr('data-effect-toggle');
    var patch = {};
    patch[key] = !node[key];
    this.updateSelectedEffectNode(patch, 'rerender');
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
    project.timeline[i].picture_frequency = 1;
    project.timeline[i].accelerometer = false;
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
