function IgnisResizer(ignis)
{
    this.ignis = ignis;
    ignis.resizer = this;

    this.thumb_batch_processes = [];
    this.tex_batch_processes = [];
    this.export_batch_processes = [];
    this.thumb_batch_callback = null;
    this.tex_batch_callback = null;
    this.export_batch_callback = null;
}

IgnisResizer.prototype.init = function ()
{
}

IgnisResizer.prototype.execFile = function (cmd, params, callback)
{
    window.electronApi.execFile(cmd, params).then(function (result) {
        if (callback) {
            callback(result.error, result.stdout, result.stderr);
        }
    });
}

IgnisResizer.prototype.thumb = function (from, to, max_size, callback)
{
    this.thumbBatch([{from: from, to: to}], max_size, callback);
}

IgnisResizer.prototype.export = function (from, to, max_size, callback)
{
    this.exportBatch([{from: from, to: to}], max_size, callback);
}

IgnisResizer.prototype.tex = function (from, to, leds, quality, callback)
{
    this.texBatch([{from: from, to: to}], leds, quality, callback);
}

IgnisResizer.prototype.thumbBatch = function (batch, max_size, callback)
{
    if (window.electronApi.platform == "darwin" || config.forcePlatform == "darwin") {
        return this.thumbBatchMac(batch, max_size, callback);
    }

    var cmd = "resizer.exe";
    var params = ['thumb', max_size];

    var cfg_file = this.prepareBatchConfig(batch);
    params.push(cfg_file);

    this.execFile(cmd, params, callback);
}

IgnisResizer.prototype.prepareBatchConfig = function (batch)
{
    var tmp = [];
    for (var o of batch) {
        tmp.push(o.from + '|' + o.to);
    }

    var resizer_dir = ignis_dir('resizer');
    var cfg_hash = window.electronApi.md5(Math.round((new Date()).getTime() / 1000) + '_' + Math.random() * 10000);
    var cfg_file = resizer_dir + path.sep + cfg_hash + '.batch';
    fs.writeFileSync(cfg_file, tmp.join('\n'));

    return cfg_file;
}

IgnisResizer.prototype.exportBatch = function (batch, max_size, callback)
{
    if (window.electronApi.platform == "darwin" || config.forcePlatform == "darwin") {
        return this.exportBatchMac(batch, max_size, callback);
    }

    var cmd = "resizer.exe";
    var params = ['export', max_size];

    var cfg_file = this.prepareBatchConfig(batch);
    params.push(cfg_file);

    this.execFile(cmd, params, callback);
}

IgnisResizer.prototype.texBatch = function (batch, leds, quality, callback)
{
    if (window.electronApi.platform == "darwin" || config.forcePlatform == "darwin") {
        return this.texBatchMac(batch, leds, quality, callback);
    }

    if (!quality) quality = config.rendering.texture_quality;

    var cmd = "resizer.exe";
    var params = ['tex', leds, quality];

    var cfg_file = this.prepareBatchConfig(batch);
    params.push(cfg_file);

    this.execFile(cmd, params, callback);
}

IgnisResizer.prototype.thumbBatchMac = function (batch, max_size, callback)
{
    this.thumb_batch_callback = callback;
    var cmd = config.magick_darwin;

    for (let i in batch) {
        var from = batch[i].from;
        var to = batch[i].to;

        var params = [
            from,
            '-resize', '300x300',
            to
        ];

        this.thumb_batch_processes.push(i);
        this.execFile(cmd, params, $.proxy(function () {
            this.thumbBatchExit(i);
        }, this));
    }
}

IgnisResizer.prototype.thumbBatchExit = function (process)
{
    var that = this;

    that.thumb_batch_processes.splice(that.thumb_batch_processes.indexOf(process), 1);
    if (that.thumb_batch_processes.length == 0) {
        if (that.thumb_batch_callback) {
            that.thumb_batch_callback();
            that.thumb_batch_callback = null;
        }
    }
}

IgnisResizer.prototype.exportBatchMac = function (batch, max_size, callback)
{
    this.export_batch_callback = callback;
    var cmd = config.magick_darwin;

    for (let i in batch) {
        var from = batch[i].from;
        var to = batch[i].to;

        var params = [
            from,
            '-resize', 'x' + max_size,
            to
        ];

        this.export_batch_processes.push(i);
        this.execFile(cmd, params, $.proxy(function () {
            this.thumbExportExit(i);
        }, this));
    }
}

IgnisResizer.prototype.thumbExportExit = function (process)
{
    var that = this;

    that.export_batch_processes.splice(that.export_batch_processes.indexOf(process), 1);
    if (that.export_batch_processes.length == 0) {
        if (that.export_batch_callback) {            
            that.export_batch_callback();
            that.export_batch_callback = null;
        }
    }
}

IgnisResizer.prototype.texBatchMac = function (batch, leds, quality, callback)
{
    this.tex_batch_callback = callback;
    var cmd = config.magick_darwin;

    for (let i in batch) {
        var from = batch[i].from;
        var to = batch[i].to;

        var params = [
            from,
            '-resize', 'x' + leds,
            '-interpolate', 'Nearest',
            '-filter', 'point',
            '-resize', '512x512!',
            to
        ];

        this.tex_batch_processes.push(i);
        this.execFile(cmd, params, $.proxy(function () {
            this.thumbTexExit(i);
        }, this));
    }
}

IgnisResizer.prototype.thumbTexExit = function (process)
{
    var that = this;

    that.tex_batch_processes.splice(that.tex_batch_processes.indexOf(process), 1);
    if (that.tex_batch_processes.length == 0) {
        if (that.tex_batch_callback) {
            that.tex_batch_callback();
            that.tex_batch_callback = null;
        }
    }
}

IgnisResizer.prototype.resize = function (mode, from, to, width, height, callback)
{
    if (!mode) mode = 'single';

    if (window.electronApi.platform == "darwin" || config.forcePlatform == "darwin") {
        return this.resizeMac( mode, from, to, width, height, callback );
    }

    var cmd = "resizer.exe";
    var params = [mode, from, to, width, height];

    this.execFile(cmd, params, callback);
}

IgnisResizer.prototype.resizeMac = function (mode, from, to, width, height, callback)
{

}

IgnisResizer.prototype.stickMirrorGap = function (hash, leds, gap, rotate, reverse, callback)
{
    if (typeof reverse == 'function') {
        callback = reverse;
        reverse = false;
    }

    reverse = !!reverse;

    var mr = (rotate ? '_r' : '_m') + (reverse ? 'v' : '');
    var fn_tex = ignis_dir('tex') + path.sep + hash + mr + gap + '_' + leds + '.jpg';
    var fn_mir = ignis_dir('mirror') + path.sep + hash + mr + gap + '_' + leds + '.png';
    

    if (gap % 2 != 0) gap += 1;
    var height = (leds - gap) / 2;

    var i = this.ignis.library.getImageByHash(hash);
    var width = Math.round(height * i.resolution.r);
    
    var from = i.path;
    var to = ignis_dir('tmp') + path.sep + hash + '_h_' + width + '_' + height;
    
    this.resize('single', from, to + '.png', width, height, $.proxy(function () {
        this.resize((rotate ? 'rotate' : 'flip'), from, to + (rotate ? '_rotate.png' : '_flip.png'), width, height, $.proxy(function () {
            var top = to + '.png';
            var bot = to + (rotate ? '_rotate.png' : '_flip.png');

            var img_top = new Image();
            img_top.onload = $.proxy(function () {
                var img_bot = new Image();
                img_bot.onload = $.proxy(function () {
                    var canvas = document.createElement('canvas');
                    canvas.width = width;
                    canvas.height = 2 * height + gap;
                    var ctx = canvas.getContext('2d');
                    ctx.fillRect(0, 0, canvas.width, canvas.height);
                    if (reverse) {
                        ctx.drawImage(img_bot, 0, 0);
                        ctx.drawImage(img_top, 0, height + gap);
                    } else {
                        ctx.drawImage(img_top, 0, 0);
                        ctx.drawImage(img_bot, 0, height + gap);
                    }
            
                    var data = canvas.toDataURL("image/png").split(',', 2)[1];
                    var buff = Buffer.from(data, 'base64');
                    fs.writeFileSync(fn_mir, buff);

                    this.resize('point', fn_mir, fn_tex, 512, 512, $.proxy(function () {
                        if (callback) callback();
                        console.log('MIRRORED!');
                    }, this));
                }, this);
                img_bot.src = bot;
            }, this);
            img_top.src = top;
        }, this));
    }, this));
}
