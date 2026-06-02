function IgnisAudio(ignis)
{
    this.ignis = ignis;
    this.ignis.audio = this;

    this.audioCtx = null;
    this.analyser = null;
    this.audioBufferSourceNode = null;
    this.audioBuffer = null;
    this.canvas = null;
    this.envelopeBuffers = [];

    this.started = false;

    this.startedAt = 0;
    this.pausedAt = 0;

    this.autostart = false;
}

/**
 * Initialize audio system
 */
IgnisAudio.prototype.init = function ()
{
    this.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    this.audioCtx.audioObject = this;

    this.analyser = this.audioCtx.createAnalyser();
    
    this.audioBufferSourceNode = this.audioCtx.createBufferSource();
    
    this.audioBufferSourceNode.connect(this.analyser);
    this.analyser.connect(this.audioCtx.destination);

    this.audioBuffer = null;
    this.audioData = null;
}

IgnisAudio.prototype.play = function (position) 
{
    if (this.started) this.audioBufferSourceNode.stop();

    var pause = 0;
    var opos = position;
    position = position - this.ignis.project.audio_offset;

    if (position < 0) {
        pause = Math.abs(position);
        position = 0;
    }

    this.audioBufferSourceNode = this.audioCtx.createBufferSource();
    this.audioBufferSourceNode.connect(this.analyser);
    this.audioBufferSourceNode.buffer = this.audioBuffer;
    this.audioBufferSourceNode.start(this.audioCtx.currentTime + pause / 1000, position / 1000);
    this.started = true;
    this.startedAt = this.audioCtx.currentTime - (opos / 1000);
    this.pausedAt = -1;
}

IgnisAudio.prototype.getPosition = function ()
{
    if (!this.audioCtx) return -1;
    var ct = (this.pausedAt > 0 ? this.pausedAt : this.audioCtx.currentTime);
    return ct - this.startedAt;
}

IgnisAudio.prototype.pause = function () 
{
    if (!this.started) return;
    this.pausedAt = this.audioCtx.currentTime;
    this.audioBufferSourceNode.stop();
    this.started = false;
}

/**
 * Load and process mp3 file
 * 
 * @param   fn  file path of mp3 to load
 */
IgnisAudio.prototype.loadFile = function (fn, autostart)
{
    $('#audio-loading-overlay').show();
    this.autostart = autostart;
    var request = new XMLHttpRequest();
	request.open( 'GET', fn, true );
	request.responseType = 'arraybuffer';
    request.onload = this.onFileLoaded;
    request.audioObject = this;
    request.send();
}

/**
 * When mp3 file is loaded, proceed with audio decoding
 * 
 * @param   e   process event
 */
IgnisAudio.prototype.onFileLoaded = function (e)
{
    this.audioObject.audioData = this.response;
    this.audioObject.audioCtx.decodeAudioData(this.audioObject.audioData, this.audioObject.onAudioDataDecoded, this.audioObject.onAudioDataDecodeError);
}

/**
 * Process raw arraybuffer into audio buffer
 * 
 * @param   buffer
 */
IgnisAudio.prototype.onAudioDataDecoded = function (buffer)
{
    this.audioObject.audioBufferSourceNode = this.audioObject.audioCtx.createBufferSource();
    this.audioObject.audioBufferSourceNode.connect(this.audioObject.analyser);
    this.audioObject.audioBufferSourceNode.buffer = buffer;
    //this.audioObject.audioBufferSourceNode.start();
    this.audioObject.audioBuffer = buffer;
    var data = this.audioObject.audioBuffer.getChannelData(0);
    var wave = this.audioObject.getWaveformData(buffer, buffer.duration * 1000);

    this.audioObject.renderEnvelopeBuffers(wave, config.audio.envelope_width, config.audio.envelope_height);
}

/**
 * Render buffers for displaying audio envelope
 * 
 * @param   wave    object containing left and right channels of audio as float arrays
 * @param   width   width of single buffer image
 * @param   height  height of single buffer image
 */
IgnisAudio.prototype.renderEnvelopeBuffers = function (wave, width, height)
{
    const bufflen = width;
    const cnt = wave.left.length;
    const buffers = Math.ceil(cnt / bufflen);
    this.envelopeBuffers = [];

    for (let i = 0; i < buffers; i++) {
        const buffer = document.createElement('canvas');
        buffer.width = bufflen;
        buffer.height = height;
        const ctx = buffer.getContext('2d');
        ctx.clearRect(0, 0, bufflen, height);
        ctx.lineWidth = 1;

        ctx.strokeStyle = config.audio.envelope_color;
        ctx.beginPath();
        for (let l = 0; l < bufflen; l++) {
            //ctx.moveTo(l+0.5, height / 2);
            //ctx.lineTo(l+0.5, height / 2 + wave.left[l+i*bufflen] * (height / 2));
            ctx.moveTo(l+0.5, height / 2);
            ctx.lineTo(l+0.5, height / 2 - wave.right[l+i*bufflen] * (height / 2));
        }
        ctx.stroke();

        ctx.strokeStyle = config.audio.envelope_color_alt;
        ctx.beginPath();
        for (let l = 0; l < bufflen; l++) {
            ctx.moveTo(l+0.5, height / 2);
            ctx.lineTo(l+0.5, height / 2 + wave.left[l+i*bufflen] * (height / 2));
            //ctx.moveTo(l+0.5, height / 2);
            //ctx.lineTo(l+0.5, height / 2 - wave.right[l+i*bufflen] * (height / 2));
        }
        ctx.stroke();

        this.envelopeBuffers.push(buffer);
    }

    if (this.autostart) {
        this.play(this.autostart);
        this.autostart = false;
    }

    $('#audio-loading-overlay').hide();
    app_execute_event('audio_rendered');
}

IgnisAudio.prototype.clear = function ()
{
    if (!this.audioCtx) return;
    this.audioBuffer = null;
    this.audioData = null;
    this.audioBufferSourceNode = this.audioCtx.createBufferSource();    
    this.audioBufferSourceNode.connect(this.analyser);
    this.envelopeBuffers = [];
    app_execute_event('audio_rendered');
}

/**
 * Called when there was an error decofing audio file
 */
IgnisAudio.prototype.onAudioDataDecodeError = function (e)
{
    console.log(e);
}

/**
 * Generate wave object containing normalized float data for each chanels
 * 
 * @param   audioBuffer buffer with raw audio samples
 * @param   dataPoints  target resolution 
 */
IgnisAudio.prototype.getWaveformData = function (audioBuffer, dataPoints)
{
    const leftChannel = audioBuffer.getChannelData(0);
    const rightChannel = audioBuffer.getChannelData(1);
    const values = {
        left: new Float32Array(dataPoints),
        right: new Float32Array(dataPoints)
    };

    const avg = values => values.reduce((sum, value) => sum + value, 0) / values.length;

    var max = 0;

    const dataWindow = Math.round(leftChannel.length / dataPoints);
    for (let i = 0, y = 0, lbuffer = [], rbuffer = []; i < leftChannel.length; i++) {
        const lsum = Math.abs(leftChannel[i]);
        const rsum = Math.abs(rightChannel[i]);
        const summedValue = (Math.abs(leftChannel[i]) + Math.abs(rightChannel[i])) / 2;
        lbuffer.push(lsum);
        rbuffer.push(rsum);
        if (lbuffer.length === dataWindow) {
            const rv = avg(rbuffer);
            const lv = avg(lbuffer);
            values.right[y] = rv;
            values.left[y++] = lv;
            if (lv > max) max = lv;
            if (rv > max) max = rv;
            lbuffer = [];
            rbuffer = [];
        }
    }

    // normalize
    for (let i = 0; i < values.left.length; i++) {
        values.left[i] = values.left[i] / max;
        values.right[i] = values.right[i] / max;
    }

    return values;
}
